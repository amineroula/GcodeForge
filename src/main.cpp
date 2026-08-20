// Step 4: a real editor -- ImGui panels for objects, transform, layers,
// selection groups, speed editing, color mode, a bed panel, undo/redo, and
// viewport path selection, plus File > Open for real SRC/G-code files.
//
// Viewport controls are Maya-style: Alt+left-drag orbits, Alt+middle-drag
// pans, Alt+right-drag (or plain scroll, no Alt needed) zooms/dollies.
// The active object always shows a move gizmo (red/green/blue arrows at
// its pivot) -- clicking and dragging an arrow takes priority over path
// selection, so plain click/drag only selects paths when it DOESN'T land
// on an arrow. Plain click-drag elsewhere marquee-selects everything
// inside the dragged rectangle. Shift adds to the selection, Ctrl
// subtracts. Keys 1/2/3/4 jump to Top/Front/Right/Iso views, key G toggles
// the reference grid, Ctrl+Z/Ctrl+Y undo/redo. Mouse/keys are ignored by
// the viewport whenever ImGui wants them.

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <map>
#include <optional>
#include <tuple>

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include "editor/ConnectedDrag.h"
#include "editor/Framing.h"
#include "editor/Gizmo.h"
#include "editor/Picking.h"
#include "editor/Selection.h"
#include "editor/SrcExporter.h"
#include "editor/UndoStack.h"
#include "io/BedIO.h"
#include "io/FileIO.h"
#include "model/BedHeightmap.h"
#include "model/Scene.h"
#include "parser/SrcParser.h"
#include "parser/GcodeParser.h"
#include "render/BedHeightmapRenderer.h"
#include "render/BedSettings.h"
#include "render/Camera.h"
#include "render/GeometryRenderer.h"
#include "render/GizmoRenderer.h"
#include "render/GridRenderer.h"
#include "render/LightingSettings.h"
#include "render/PathColorizer.h"
#include "render/RenderSettings.h"
#include "render/SceneRenderer.h"
#include "render/SelectionHighlightRenderer.h"
#include "ui/EditorUI.h"
#include "ui/FileDialog.h"

namespace {

Camera* g_camera = nullptr;
Scene* g_scene = nullptr;
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
        case GLFW_KEY_T: if (g_camera) g_camera->setPreset(Camera::Preset::Top); break;
        case GLFW_KEY_P: if (g_camera) g_camera->setProjection(Camera::Projection::Perspective); break;
        case GLFW_KEY_U: if (g_camera) g_camera->setProjection(Camera::Projection::Orthographic); break;
        case GLFW_KEY_F:
            // "F" was requested for both "frame selection" and "front view" --
            // frame wins (listed first, and matches Maya/Blender convention);
            // front view stays on key 2 and the View panel's Front button.
            if (g_camera && g_scene) {
                if (auto bounds = computeFrameBounds(*g_scene, /*preferSelection=*/true)) {
                    g_camera->frameBounds(bounds->center, bounds->radius);
                }
            }
            break;
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
    // Every SceneObject starts with the same hardcoded default color (see
    // SceneObject.h) -- fine for a single object, but it made "Color mode:
    // Object" look broken once a second file was loaded, since both
    // objects rendered identically until the operator manually recolored
    // one. Give each newly-loaded object the next color in the shared
    // palette instead, indexed by load order, so Object mode distinguishes
    // objects out of the box. Still just a starting point -- the object
    // list's color swatch can still override it per object.
    object.color = colorPalette()[scene.objects.size() % colorPalette().size()];
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
    std::setvbuf(stdout, nullptr, _IONBF, 0); // unbuffered -- so redirected/killed-early output is still visible (debugging real-file loads)
    glfwSetErrorCallback(onGlfwError);

    if (!glfwInit()) {
        std::fprintf(stderr, "Failed to initialize GLFW\n");
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    // 4x MSAA for smoother line/edge rendering ("sampling options for
    // better line drawing"). This is a window-creation-time hint in
    // vanilla GLFW -- changing the sample count at runtime would require
    // destroying and recreating the window/context, so this is fixed for
    // now rather than a live UI toggle.
    glfwWindowHint(GLFW_SAMPLES, 4);

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

    // The default ImGui font (a small bitmap font baked for debug tools)
    // is a big part of why a plain ImGui app "looks weak" -- swap in a
    // real system font. Segoe UI ships on every stock Windows install, so
    // this is safe to hardcode for a Windows-only app; AddFontFromFileTTF
    // returns nullptr (not a crash) if the path is ever wrong, so the
    // fallback to ImGui's built-in font still works if something's off.
    ImGuiIO& io = ImGui::GetIO();
    ImFont* regularFont = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 17.0f);
    ImFont* boldFont = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeuib.ttf", 18.0f);
    if (!regularFont) io.Fonts->AddFontDefault();

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.ChildRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 6.0f;
    style.WindowPadding = ImVec2(10.0f, 10.0f);
    style.FramePadding = ImVec2(6.0f, 4.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);
    style.ScrollbarSize = 14.0f;
    style.WindowTitleAlign = ImVec2(0.02f, 0.5f);

    ImGui_ImplGlfw_InitForOpenGL(window, false); // false: we install and forward callbacks ourselves, see the note above onScroll
    ImGui_ImplOpenGL3_Init("#version 330");

    Camera camera;
    g_camera = &camera;

    GridRenderer grid;
    SceneRenderer sceneRenderer;
    GeometryRenderer geometryRenderer;
    SelectionHighlightRenderer selectionHighlight;
    GizmoRenderer gizmoRenderer;
    BedHeightmapRenderer bedHeightmapRenderer;
    Scene scene;
    g_scene = &scene;
    EditorUI editorUi;
    editorUi.setBoldFont(boldFont);
    UndoStack undoStack;
    ColorMode colorMode = ColorMode::Layer;
    RenderSettings renderSettings;
    BedSettings bedSettings;
    LightingSettings lightingSettings;
    BedHeightmap bedHeightmap;

    grid.rebuild(bedSettings);
    bedHeightmap.resizeToBed(bedSettings.widthMm, bedSettings.depthMm);
    bedHeightmapRenderer.rebuild(bedSettings, bedHeightmap);

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

        // Sanity-check the outline mesh too (can't click a path to select
        // it from here): select every print path on the sample object,
        // rebuild, and confirm the outline mesh actually builds.
        if (SceneObject* sample = scene.activeObject()) {
            for (const auto& path : sample->paths) {
                if (path.type == PathType::Print) sample->selectedPaths.insert(path.number);
            }
            geometryRenderer.rebuild(scene, colorMode, renderSettings.beadWidthMm, renderSettings.beadHeightMm);
            GLenum outlineGlError = glGetError();
            std::printf("Outline mesh sanity check: %zu triangle(s) built, glGetError=%d (0=GL_NO_ERROR)\n",
                        geometryRenderer.outlineTriangleCount(), static_cast<int>(outlineGlError));
            sample->selectedPaths.clear();
            geometryRenderer.rebuild(scene, colorMode, renderSettings.beadWidthMm, renderSettings.beadHeightMm);
        }

        // Sanity-check the bed heightmap mesh too: put some non-zero
        // elevations in and confirm it actually builds without a GL error.
        for (size_t i = 0; i < bedHeightmap.elevationsMm.size(); ++i) {
            bedHeightmap.elevationsMm[i] = (static_cast<float>(i % 5) - 2.0f) * 0.5f;
        }
        bedHeightmapRenderer.rebuild(bedSettings, bedHeightmap);
        GLenum heightmapGlError = glGetError();
        std::printf("Bed heightmap sanity check: %d x %d grid, %zu triangle(s) built, glGetError=%d (0=GL_NO_ERROR)\n",
                    bedHeightmap.cols, bedHeightmap.rows, bedHeightmapRenderer.triangleCount(), static_cast<int>(heightmapGlError));
        std::fill(bedHeightmap.elevationsMm.begin(), bedHeightmap.elevationsMm.end(), 0.0f);
        bedHeightmapRenderer.rebuild(bedSettings, bedHeightmap);
    }

    // Debug/testing convenience: load an extra file at startup if
    // GCODEFORGE_TEST_FILE is set, and time parse+rebuild for both
    // renderers. Not part of the UI -- purely for validating against real,
    // large files from a script without needing to click File > Open.
    if (const char* extraFile = std::getenv("GCODEFORGE_TEST_FILE")) {
        auto t0 = std::chrono::steady_clock::now();
        loadFileIntoScene(extraFile, scene);
        auto t1 = std::chrono::steady_clock::now();
        sceneRenderer.rebuild(scene, colorMode);
        auto t2 = std::chrono::steady_clock::now();
        geometryRenderer.rebuild(scene, colorMode, renderSettings.beadWidthMm, renderSettings.beadHeightMm);
        auto t3 = std::chrono::steady_clock::now();
        auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
        std::printf("GCODEFORGE_TEST_FILE: parse=%.1fms  lines-rebuild=%.1fms  geometry-rebuild=%.1fms\n",
                    ms(t0, t1), ms(t1, t2), ms(t2, t3));
        std::printf("  lines: %zu segments, geometry: %zu triangles\n",
                    sceneRenderer.lineCount(), geometryRenderer.triangleCount());

        // Real-world round-trip validation: export the untouched object
        // and diff it against the original file line-by-line. Should be
        // byte-identical (zero patches, zero insertions) since nothing
        // was edited -- this is the actual production file, not the
        // synthetic hand-written test snippet.
        if (!scene.objects.empty()) {
            auto t4 = std::chrono::steady_clock::now();
            ExportResult exportResult;
            std::vector<std::string> exportedLines = buildExportedLines(scene.objects.back(), exportResult);
            auto t5 = std::chrono::steady_clock::now();
            std::vector<std::string> originalLines = readLinesFromFile(extraFile);
            bool identical = (exportedLines == originalLines);
            std::printf("Round-trip export: %.1fms, patched=%d, insertedSpeed=%d, insertedActions=%d, "
                        "lineCountMatch=%s, byteIdentical=%s\n",
                        std::chrono::duration<double, std::milli>(t5 - t4).count(),
                        exportResult.patchedCoordinateLines, exportResult.insertedSpeedLines, exportResult.insertedLayerActions,
                        (exportedLines.size() == originalLines.size()) ? "yes" : "no",
                        identical ? "yes" : "no");
            if (!identical) {
                for (size_t i = 0; i < std::min(exportedLines.size(), originalLines.size()); ++i) {
                    if (exportedLines[i] != originalLines[i]) {
                        std::printf("  first diff at line %zu:\n    original: %s\n    exported: %s\n",
                                    i, originalLines[i].c_str(), exportedLines[i].c_str());
                        break;
                    }
                }
            }
        }
        std::fflush(stdout);
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
    glEnable(GL_MULTISAMPLE);
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

    // Move-gizmo drag state. axisOrigin/StartT are captured ONCE at drag
    // start and held fixed for the whole drag -- see the comment on
    // closestPointOnAxisToRay in editor/Gizmo.h for why a fixed reference
    // point is required, not a per-frame recomputed one.
    std::optional<GizmoAxis> gizmoDragAxis;
    GizmoTargetMode gizmoDragMode = GizmoTargetMode::Object;
    glm::vec3 gizmoDragAxisOrigin(0.0f);
    float gizmoDragStartT = 0.0f;
    double gizmoDragStartValue = 0.0; // Object mode only
    // Start/End/Whole modes only: each affected path's from/to as they
    // were at drag start (selected paths AND any connected unselected
    // neighbor -- see editor/ConnectedDrag.h), so the delta is always
    // applied from a fixed base rather than accumulated frame-to-frame
    // (which would drift).
    std::vector<PathDragSnapshot> gizmoDragPathSnapshots;
    constexpr float kGizmoPickRadiusPixels = 10.0f;

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
        editorUi.draw(scene, colorMode, camera, renderSettings, bedSettings, lightingSettings, bedHeightmap, undoStack,
                      renderedPrimitiveCount, sceneDirty, selectionDirty, bedDirty);

        if (editorUi.openFileRequested()) {
            editorUi.clearOpenFileRequest();
            if (auto path = showOpenSrcDialog(window)) {
                loadFileIntoScene(*path, scene);
                sceneDirty = true;
            }
        }
        if (editorUi.saveBedRequested()) {
            editorUi.clearSaveBedRequest();
            if (auto path = showSaveBedDialog(window)) {
                if (!saveBedSettings(*path, bedSettings, bedHeightmap)) {
                    std::fprintf(stderr, "Could not save bed settings to: %s\n", path->c_str());
                }
            }
        }
        if (editorUi.loadBedRequested()) {
            editorUi.clearLoadBedRequest();
            if (auto path = showOpenBedDialog(window)) {
                if (loadBedSettings(*path, bedSettings, bedHeightmap)) {
                    bedDirty = true;
                } else {
                    std::fprintf(stderr, "Could not load bed settings from: %s\n", path->c_str());
                }
            }
        }
        if (editorUi.saveSrcRequested()) {
            editorUi.clearSaveSrcRequest();
            if (SceneObject* active = scene.activeObject()) {
                if (auto path = showSaveSrcDialog(window, active->name + ".src")) {
                    ExportResult exportResult = exportSrcToFile(*active, *path);
                    if (exportResult.success) {
                        std::printf("Exported %s: %d coordinate patch(es), %d speed insertion(s), %d layer action(s)\n",
                                    path->c_str(), exportResult.patchedCoordinateLines,
                                    exportResult.insertedSpeedLines, exportResult.insertedLayerActions);
                    } else {
                        std::fprintf(stderr, "Export failed: %s\n", exportResult.errorMessage.c_str());
                    }
                }
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

        // leftPressed/leftWasPressed are tracked unconditionally, every
        // frame, regardless of altHeld or viewportInputActive -- if this
        // edge-detection state only updated inside the branches below (as
        // an earlier version of this code did), toggling Alt mid-drag
        // could leave it stuck, which combined with isDraggingMarquee
        // never being reset after a completed drag caused the marquee
        // rectangle to reappear and track the cursor during an unrelated
        // Alt+LMB camera orbit (Alt+LMB holds the left button too) --
        // exactly the reported "selection box keeps showing up" bug.
        bool leftPressed = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;

        if (altHeld && viewportInputActive) {
            if (leftPressed) {
                camera.orbit(static_cast<float>(dx), static_cast<float>(dy));
            } else if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS) {
                camera.pan(static_cast<float>(dx), static_cast<float>(dy));
            } else if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
                camera.zoom(static_cast<float>(-dy) * 0.05f); // drag down = zoom out, matching Maya's Alt+RMB dolly
            }
            isDraggingMarquee = false; // Alt is for camera nav -- never leave a marquee armed while it's held
            gizmoDragAxis.reset(); // and never leave a gizmo drag armed either
        } else if (viewportInputActive) {
            glm::vec2 current(static_cast<float>(cursorX), static_cast<float>(cursorY));
            SceneObject* active = scene.activeObject();
            // Start/End/Whole only actually apply with a non-empty
            // selection; otherwise there's nothing for them to edit, so
            // fall back to moving the whole object -- matches
            // computeGizmoOrigin's own fallback, keeping "where the gizmo
            // is drawn" and "what dragging it does" in agreement.
            GizmoTargetMode effectiveMode = (active && !active->selectedPaths.empty())
                ? renderSettings.gizmoMode : GizmoTargetMode::Object;

            // The move gizmo takes priority over path selection: if the
            // click lands on an arrow, drag the object/paths; only fall
            // through to click/marquee-select when it doesn't.
            if (leftPressed && !leftWasPressed) {
                bool gizmoHit = false;
                if (active) {
                    if (auto origin = computeGizmoOrigin(*active, effectiveMode)) {
                        ScreenProjector projector{viewProj, static_cast<float>(width), static_cast<float>(height)};
                        std::vector<GizmoAxisScreenSegment> segments;
                        for (GizmoAxis axis : {GizmoAxis::X, GizmoAxis::Y, GizmoAxis::Z}) {
                            glm::vec3 tip = *origin + gizmoAxisDirection(axis) * GizmoRenderer::kAxisLengthMm;
                            auto originScreen = projector.project(*origin);
                            auto tipScreen = projector.project(tip);
                            if (originScreen && tipScreen) {
                                segments.push_back({axis, glm::vec2(*originScreen), glm::vec2(*tipScreen)});
                            }
                        }
                        if (auto axisHit = pickGizmoAxis(segments, current, kGizmoPickRadiusPixels)) {
                            Ray ray = unprojectRay(viewProj, current, static_cast<float>(width), static_cast<float>(height));
                            glm::vec3 axisDir = gizmoAxisDirection(*axisHit);
                            if (auto t = closestPointOnAxisToRay(*origin, axisDir, ray)) {
                                gizmoDragAxis = axisHit;
                                gizmoDragMode = effectiveMode;
                                gizmoDragAxisOrigin = *origin;
                                gizmoDragStartT = *t;
                                gizmoDragPathSnapshots.clear();
                                if (effectiveMode == GizmoTargetMode::Object) {
                                    gizmoDragStartValue = (*axisHit == GizmoAxis::X) ? active->transform.x
                                                         : (*axisHit == GizmoAxis::Y) ? active->transform.y
                                                                                       : active->transform.z;
                                } else {
                                    // Includes the selected paths AND any connected
                                    // unselected neighbor, so the drag doesn't tear
                                    // the path away from what it was touching.
                                    gizmoDragPathSnapshots = buildDragSnapshots(*active, effectiveMode);
                                }
                                undoStack.beginContinuousEdit(scene);
                                gizmoHit = true;
                            }
                        }
                    }
                }
                if (!gizmoHit) {
                    mouseDownPos = current;
                    isDraggingMarquee = false;
                }
            } else if (leftPressed && leftWasPressed) {
                if (gizmoDragAxis && active) {
                    Ray ray = unprojectRay(viewProj, current, static_cast<float>(width), static_cast<float>(height));
                    glm::vec3 axisDir = gizmoAxisDirection(*gizmoDragAxis);
                    if (auto t = closestPointOnAxisToRay(gizmoDragAxisOrigin, axisDir, ray)) {
                        float deltaScalar = *t - gizmoDragStartT;
                        if (gizmoDragMode == GizmoTargetMode::Object) {
                            double newValue = gizmoDragStartValue + deltaScalar;
                            if (*gizmoDragAxis == GizmoAxis::X) active->transform.x = newValue;
                            else if (*gizmoDragAxis == GizmoAxis::Y) active->transform.y = newValue;
                            else active->transform.z = newValue;
                        } else {
                            // The gizmo's axes are WORLD axes, but Path::from/to
                            // are stored in the object's LOCAL space -- convert
                            // the world-space drag delta back before applying it.
                            glm::vec3 worldDelta = axisDir * deltaScalar;
                            glm::dvec3 localDelta = inverseTransformDelta(active->transform, glm::dvec3(worldDelta));
                            // Each snapshot carries its OWN moveFrom/moveTo flags
                            // (not the global gizmoDragMode) -- a connected
                            // neighbor that got pulled in only moves the ONE
                            // endpoint that was actually touching the selection,
                            // not both.
                            for (const auto& snap : gizmoDragPathSnapshots) {
                                if (Path* path = active->findPath(snap.pathNumber)) {
                                    if (snap.moveFrom) path->from = snap.startFrom + localDelta;
                                    if (snap.moveTo) path->to = snap.startTo + localDelta;
                                }
                            }
                        }
                        sceneDirty = true;
                    }
                } else if (glm::length(current - mouseDownPos) > kDragThresholdPixels) {
                    isDraggingMarquee = true;
                }
            } else if (!leftPressed && leftWasPressed) {
                if (gizmoDragAxis) {
                    undoStack.commitContinuousEdit();
                    gizmoDragAxis.reset();
                    gizmoDragPathSnapshots.clear();
                } else {
                    ScreenProjector projector{viewProj, static_cast<float>(width), static_cast<float>(height)};
                    SelectionCompose compose = currentSelectionCompose();
                    undoStack.snapshotBeforeChange(scene); // selection changes are undoable too, same as any other edit

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
                        auto hit = pickNearestPath(scene, projector, current, kClickPickRadiusPixels, renderSettings.selectBackfacing);
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
                isDraggingMarquee = false; // drag is over -- must reset, or a later Alt+LMB orbit re-triggers the overlay (see note above)
            }
        }
        leftWasPressed = leftPressed;

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
            selectionHighlight.rebuild(scene, renderSettings.mode);
            // GeometryRenderer bakes selectionHighlightColor() directly
            // into its mesh vertex colors (the separate overlay above
            // can't show through solid bead geometry -- see
            // SelectionHighlightRenderer.h). That means a PURE selection
            // change, with no structural change, still needs to re-upload
            // the mesh when Geometry mode is active, or the just-selected
            // path never actually turns green until something else
            // happens to trigger a full rebuild. Lines mode doesn't pay
            // this cost -- it relies entirely on the (cheap) overlay.
            if (renderSettings.mode == RenderMode::Geometry) {
                geometryRenderer.rebuild(scene, colorMode, renderSettings.beadWidthMm, renderSettings.beadHeightMm);
            }
        }
        if (bedDirty) {
            grid.rebuild(bedSettings);
            // Reused for heightmap edits too (spacing, resize, per-point
            // values, visibility) -- not just bed size/position -- so the
            // heightmap mesh needs a resize-to-current-bed-extent pass
            // (a no-op if the bed itself didn't change) before rebuilding.
            bedHeightmap.resizeToBed(bedSettings.widthMm, bedSettings.depthMm);
            bedHeightmapRenderer.rebuild(bedSettings, bedHeightmap);
        }

        glClearColor(0.09f, 0.10f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (height > 0) {
            if (g_showGrid) grid.draw(viewProj);
            if (bedHeightmap.visible) bedHeightmapRenderer.draw(viewProj, lightingSettings);
            // Selection highlight draws BEFORE the real geometry, wide and
            // depth-tested normally -- the real geometry (always at least
            // as close to the camera as its own centerline) naturally
            // overwrites the highlight's color across its footprint,
            // leaving only the wide highlight's edges visible as an
            // outline/border. See SelectionHighlightRenderer.h.
            selectionHighlight.draw(viewProj);
            if (renderSettings.mode == RenderMode::Lines) {
                sceneRenderer.draw(viewProj);
            } else {
                geometryRenderer.draw(viewProj, lightingSettings, renderSettings.backfaceCulling,
                                       renderSettings.selectionStyle, static_cast<float>(glfwGetTime()));
            }

            if (SceneObject* active = scene.activeObject()) {
                // NOT active->transform.x/y/z -- that raw pivot can be
                // arbitrarily far from the actual geometry (a freshly-
                // loaded real KUKA file typically has transform=={0,0,0}
                // while its coordinates are in the thousands of mm),
                // which was the root cause of "can't see the gizmo."
                GizmoTargetMode drawMode = (!active->selectedPaths.empty()) ? renderSettings.gizmoMode : GizmoTargetMode::Object;
                if (auto origin = computeGizmoOrigin(*active, drawMode)) {
                    gizmoRenderer.rebuild(*origin); // cheap (3 arrows) -- rebuilding every frame is fine
                    gizmoRenderer.draw(viewProj);
                }
            }
        }

        // Marquee rectangle overlay, drawn in screen space on top of everything.
        // The !altHeld check is defense-in-depth on top of the state-machine
        // fix above -- isDraggingMarquee should already be false whenever
        // Alt is held, but never rendering a marquee during camera nav is
        // cheap insurance against this exact class of bug recurring.
        if (isDraggingMarquee && !altHeld && leftPressed) {
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
    g_scene = nullptr;
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
