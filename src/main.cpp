// Step 4: a real editor -- ImGui panels for objects, transform, layers,
// selection groups, speed editing, color mode, a bed panel, undo/redo, and
// viewport path selection, plus File > Open for real SRC/G-code files.
//
// Viewport controls are Maya-style: Alt+left-drag orbits, Alt+middle-drag
// pans, Alt+right-drag (or plain scroll, no Alt needed) zooms/dollies.
// Plain (no Alt) left-click selects the nearest path; plain left-drag
// marquee-selects everything inside the dragged rectangle. Shift adds to
// the selection, Ctrl subtracts. Keys 1/2/3/4 jump to Top/Front/Right/Iso
// views, key G toggles the reference grid, Ctrl+Z/Ctrl+Y undo/redo. Mouse/
// keys are ignored by the viewport whenever ImGui wants them.

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <algorithm>
#include <cstdio>
#include <string>
#include <map>

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include "editor/Picking.h"
#include "editor/Selection.h"
#include "editor/UndoStack.h"
#include "io/FileIO.h"
#include "model/Scene.h"
#include "parser/SrcParser.h"
#include "parser/GcodeParser.h"
#include "render/BedSettings.h"
#include "render/Camera.h"
#include "render/GeometryRenderer.h"
#include "render/GridRenderer.h"
#include "render/PathColorizer.h"
#include "render/RenderSettings.h"
#include "render/SceneRenderer.h"
#include "render/SelectionHighlightRenderer.h"
#include "ui/EditorUI.h"
#include "ui/FileDialog.h"

namespace {

Camera* g_camera = nullptr;
double g_lastCursorX = 0.0;
double g_lastCursorY = 0.0;
bool g_showGrid = true;

void onGlfwError(int code, const char* description) {
    std::fprintf(stderr, "GLFW error %d: %s\n", code, description);
}

void onFramebufferResize(GLFWwindow*, int width, int height) {
    glViewport(0, 0, width, height);
    if (g_camera && height > 0) {
        g_camera->setViewportSize(static_cast<float>(width), static_cast<float>(height));
    }
}

// IMPORTANT: ImGui_ImplGlfw_InitForOpenGL is called with install_callbacks=false
// (see main()), specifically because we need our own scroll/key callbacks
// for the viewport. GLFW only allows one callback per event type -- if we
// let ImGui install its own and then called glfwSetScrollCallback/
// glfwSetKeyCallback ourselves afterward (as an earlier version of this
// file did), we'd silently REPLACE ImGui's callback, and ImGui would never
// see scroll/key events again -- which is exactly why panel scrolling only
// worked via the scrollbar and not the mouse wheel. The fix is to forward
// every event to ImGui's own handler first, then run our own logic.

void onScroll(GLFWwindow* window, double xOffset, double yOffset) {
    ImGui_ImplGlfw_ScrollCallback(window, xOffset, yOffset);
    if (ImGui::GetIO().WantCaptureMouse) return;
    if (g_camera) g_camera->zoom(static_cast<float>(yOffset));
}

void onMouseButton(GLFWwindow* window, int button, int action, int mods) {
    ImGui_ImplGlfw_MouseButtonCallback(window, button, action, mods);
}

void onCursorPos(GLFWwindow* window, double x, double y) {
    ImGui_ImplGlfw_CursorPosCallback(window, x, y);
}

void onChar(GLFWwindow* window, unsigned int c) {
    ImGui_ImplGlfw_CharCallback(window, c);
}

void onKey(GLFWwindow* window, int key, int scancode, int action, int mods) {
    ImGui_ImplGlfw_KeyCallback(window, key, scancode, action, mods);
    if (action != GLFW_PRESS) return;
    if (ImGui::GetIO().WantCaptureKeyboard) return;
    switch (key) {
        case GLFW_KEY_1: if (g_camera) g_camera->setPreset(Camera::Preset::Top); break;
        case GLFW_KEY_2: if (g_camera) g_camera->setPreset(Camera::Preset::Front); break;
        case GLFW_KEY_3: if (g_camera) g_camera->setPreset(Camera::Preset::Right); break;
        case GLFW_KEY_4: if (g_camera) g_camera->setPreset(Camera::Preset::Iso); break;
        case GLFW_KEY_G: g_showGrid = !g_showGrid; break;
        default: break;
    }
}

std::string fileStem(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    size_t start = (slash == std::string::npos) ? 0 : slash + 1;
    size_t dot = path.find_last_of('.');
    size_t end = (dot == std::string::npos || dot < start) ? path.size() : dot;
    return path.substr(start, end - start);
}

std::string fileExtensionLower(const std::string& path) {
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return "";
    std::string ext = path.substr(dot + 1);
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

void loadFileIntoScene(const std::string& path, Scene& scene) {
    std::vector<std::string> lines = readLinesFromFile(path);
    if (lines.empty()) {
        std::fprintf(stderr, "Could not read file (or it's empty): %s\n", path.c_str());
        return;
    }
    std::string name = fileStem(path);
    std::string ext = fileExtensionLower(path);
    SceneObject object = (ext == "gcode" || ext == "nc") ? parseGcode(name, lines) : parseSrc(name, lines);
    std::printf("Loaded %s (%s): %zu paths, %zu layers\n",
                object.name.c_str(), path.c_str(), object.paths.size(), object.layers.size());
    scene.addObject(std::move(object));
}

SelectionCompose currentSelectionCompose() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl) return SelectionCompose::Subtract;
    if (io.KeyShift) return SelectionCompose::Add;
    return SelectionCompose::Replace;
}

void clearAllSelections(Scene& scene) {
    for (auto& object : scene.objects) object.selectedPaths.clear();
}

} // namespace

int main() {
    glfwSetErrorCallback(onGlfwError);

    if (!glfwInit()) {
        std::fprintf(stderr, "Failed to initialize GLFW\n");
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(1280, 800, "Gcode Editor (C++)", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        std::fprintf(stderr, "Failed to initialize GLEW\n");
        return 1;
    }

    std::printf("OpenGL renderer: %s\n", glGetString(GL_RENDERER));
    std::printf("OpenGL version:  %s\n", glGetString(GL_VERSION));

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, false); // false: we install and forward callbacks ourselves, see the note above onScroll
    ImGui_ImplOpenGL3_Init("#version 330");

    Camera camera;
    g_camera = &camera;

    GridRenderer grid;
    SceneRenderer sceneRenderer;
    GeometryRenderer geometryRenderer;
    SelectionHighlightRenderer selectionHighlight;
    Scene scene;
    EditorUI editorUi;
    UndoStack undoStack;
    ColorMode colorMode = ColorMode::Layer;
    RenderSettings renderSettings;
    BedSettings bedSettings;
    const glm::vec3 lightDir = glm::normalize(glm::vec3(0.4f, -0.5f, 0.8f));

    grid.rebuild(bedSettings);

    {
        // Try the sample next to the .exe first (matches how it's packaged
        // for distribution -- assets/ sits alongside gcode_editor.exe),
        // then fall back to the dev-build path (assets/ lives in the repo,
        // not next to build/Debug/gcode_editor.exe, when running locally
        // straight out of CMake's build tree).
        std::string portablePath = executableDirectory() + "/assets/samples/sample_chair.src";
        std::vector<std::string> lines = readLinesFromFile(portablePath);
        std::string samplePath = lines.empty() ? std::string(ASSETS_DIR) + "/samples/sample_chair.src" : portablePath;

        loadFileIntoScene(samplePath, scene);
        sceneRenderer.rebuild(scene, colorMode);

        // Sanity-check the geometry (bead) renderer path at startup too,
        // even though Lines is the default mode -- there's no automated
        // way to click the UI's radio button from here, so this is the
        // check that the mesh actually builds without a GL error.
        geometryRenderer.rebuild(scene, colorMode, renderSettings.beadWidthMm, renderSettings.beadHeightMm);
        GLenum geometryGlError = glGetError();
        std::printf("Geometry mode sanity check: %zu triangle(s) built, glGetError=%d (0=GL_NO_ERROR)\n",
                    geometryRenderer.triangleCount(), static_cast<int>(geometryGlError));
    }

    int fbWidth = 0, fbHeight = 0;
    glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
    camera.setViewportSize(static_cast<float>(fbWidth), static_cast<float>(fbHeight));
    glViewport(0, 0, fbWidth, fbHeight);

    glfwSetFramebufferSizeCallback(window, onFramebufferResize);
    glfwSetScrollCallback(window, onScroll);
    glfwSetKeyCallback(window, onKey);
    glfwSetMouseButtonCallback(window, onMouseButton);
    glfwSetCursorPosCallback(window, onCursorPos);
    glfwSetCharCallback(window, onChar);

    glfwGetCursorPos(window, &g_lastCursorX, &g_lastCursorY);
    glEnable(GL_DEPTH_TEST);
    glLineWidth(1.0f);

    // Selection input state (plain click = pick nearest path, plain drag =
    // marquee-select). Tracked via polling + edge detection, same approach
    // as the camera's Alt-drag handling.
    bool leftWasPressed = false;
    glm::vec2 mouseDownPos(0.0f, 0.0f);
    bool isDraggingMarquee = false;
    constexpr float kDragThresholdPixels = 4.0f;
    constexpr float kClickPickRadiusPixels = 8.0f;

    bool ctrlZWasDown = false;
    bool ctrlYWasDown = false;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        size_t renderedPrimitiveCount = (renderSettings.mode == RenderMode::Lines)
            ? sceneRenderer.lineCount()
            : geometryRenderer.triangleCount();

        bool sceneDirty = false;
        bool selectionDirty = false;
        bool bedDirty = false;
        editorUi.draw(scene, colorMode, camera, renderSettings, bedSettings, undoStack,
                      renderedPrimitiveCount, sceneDirty, selectionDirty, bedDirty);

        if (editorUi.openFileRequested()) {
            editorUi.clearOpenFileRequest();
            if (auto path = showOpenSrcDialog(window)) {
                loadFileIntoScene(*path, scene);
                sceneDirty = true;
            }
        }

        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        glm::mat4 viewMatrix = camera.viewMatrix();
        glm::mat4 projMatrix = camera.projectionMatrix(static_cast<float>(width), static_cast<float>(height));
        glm::mat4 viewProj = projMatrix * viewMatrix;

        double cursorX, cursorY;
        glfwGetCursorPos(window, &cursorX, &cursorY);
        double dx = cursorX - g_lastCursorX;
        double dy = cursorY - g_lastCursorY;
        g_lastCursorX = cursorX;
        g_lastCursorY = cursorY;

        bool altHeld = glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS ||
                       glfwGetKey(window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS;
        bool viewportInputActive = !ImGui::GetIO().WantCaptureMouse;

        if (altHeld && viewportInputActive) {
            if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
                camera.orbit(static_cast<float>(dx), static_cast<float>(dy));
            } else if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS) {
                camera.pan(static_cast<float>(dx), static_cast<float>(dy));
            } else if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
                camera.zoom(static_cast<float>(-dy) * 0.05f); // drag down = zoom out, matching Maya's Alt+RMB dolly
            }
        } else if (viewportInputActive) {
            // Plain (no Alt) left button: click-select or marquee-select.
            bool leftPressed = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
            if (leftPressed && !leftWasPressed) {
                mouseDownPos = glm::vec2(static_cast<float>(cursorX), static_cast<float>(cursorY));
                isDraggingMarquee = false;
            } else if (leftPressed && leftWasPressed) {
                glm::vec2 current(static_cast<float>(cursorX), static_cast<float>(cursorY));
                if (glm::length(current - mouseDownPos) > kDragThresholdPixels) isDraggingMarquee = true;
            } else if (!leftPressed && leftWasPressed) {
                ScreenProjector projector{viewProj, static_cast<float>(width), static_cast<float>(height)};
                SelectionCompose compose = currentSelectionCompose();
                glm::vec2 current(static_cast<float>(cursorX), static_cast<float>(cursorY));

                if (isDraggingMarquee) {
                    glm::vec2 rectMin(std::min(mouseDownPos.x, current.x), std::min(mouseDownPos.y, current.y));
                    glm::vec2 rectMax(std::max(mouseDownPos.x, current.x), std::max(mouseDownPos.y, current.y));
                    std::vector<PathRef> hits = pickPathsInRect(scene, projector, rectMin, rectMax);

                    std::map<int, std::vector<int>> byObject;
                    for (const auto& hit : hits) byObject[hit.objectId].push_back(hit.pathNumber);
                    if (compose == SelectionCompose::Replace && hits.empty()) clearAllSelections(scene);
                    for (auto& [objectId, pathNumbers] : byObject) {
                        if (SceneObject* object = scene.findObject(objectId)) {
                            applySelectionCompose(object->selectedPaths, pathNumbers, compose);
                        }
                    }
                } else {
                    auto hit = pickNearestPath(scene, projector, current, kClickPickRadiusPixels);
                    if (hit) {
                        scene.activeObjectId = hit->objectId;
                        if (SceneObject* object = scene.findObject(hit->objectId)) {
                            applySelectionCompose(object->selectedPaths, {hit->pathNumber}, compose);
                        }
                    } else if (compose == SelectionCompose::Replace) {
                        clearAllSelections(scene);
                    }
                }
                selectionDirty = true;
            }
            leftWasPressed = leftPressed;
        }

        if (!ImGui::GetIO().WantCaptureKeyboard) {
            bool ctrlHeld = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
                            glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
            bool zDown = glfwGetKey(window, GLFW_KEY_Z) == GLFW_PRESS;
            bool yDown = glfwGetKey(window, GLFW_KEY_Y) == GLFW_PRESS;
            if (ctrlHeld && zDown && !ctrlZWasDown) { undoStack.undo(scene); sceneDirty = true; }
            if (ctrlHeld && yDown && !ctrlYWasDown) { undoStack.redo(scene); sceneDirty = true; }
            ctrlZWasDown = ctrlHeld && zDown;
            ctrlYWasDown = ctrlHeld && yDown;
        }

        if (sceneDirty) {
            // Only rebuild whichever renderer is actually on screen -- no
            // reason to spend CPU building bead geometry nobody is looking
            // at, or vice versa. Matters once files get big (docs/PLAN.md
            // milestone 11 is the deeper version of this idea).
            if (renderSettings.mode == RenderMode::Lines) {
                sceneRenderer.rebuild(scene, colorMode);
            } else {
                geometryRenderer.rebuild(scene, colorMode, renderSettings.beadWidthMm, renderSettings.beadHeightMm);
            }
            selectionDirty = true; // scene content moved/changed, so highlight positions may be stale too
        }
        if (selectionDirty) {
            selectionHighlight.rebuild(scene);
        }
        if (bedDirty) {
            grid.rebuild(bedSettings);
        }

        glClearColor(0.09f, 0.10f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (height > 0) {
            if (g_showGrid) grid.draw(viewProj);
            if (renderSettings.mode == RenderMode::Lines) {
                sceneRenderer.draw(viewProj);
            } else {
                geometryRenderer.draw(viewProj, lightDir);
            }
            selectionHighlight.draw(viewProj);
        }

        // Marquee rectangle overlay, drawn in screen space on top of everything.
        if (isDraggingMarquee && glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
            ImDrawList* drawList = ImGui::GetForegroundDrawList();
            ImVec2 a(mouseDownPos.x, mouseDownPos.y);
            ImVec2 b(static_cast<float>(cursorX), static_cast<float>(cursorY));
            drawList->AddRect(a, b, IM_COL32(255, 255, 60, 255));
            drawList->AddRectFilled(a, b, IM_COL32(255, 255, 60, 40));
        }

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    g_camera = nullptr;
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
