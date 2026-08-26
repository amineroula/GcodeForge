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
#include "editor/ExportValidation.h"
#include "editor/Framing.h"
#include "editor/Gizmo.h"
#include "editor/InterleavePrint.h"
#include "editor/MirrorObject.h"
#include "editor/ObjectLinking.h"
#include "editor/Picking.h"
#include "editor/PrintAnimation.h"
#include "editor/Selection.h"
#include "editor/SrcExporter.h"
#include "editor/UndoStack.h"
#include "io/BedIO.h"
#include "io/ProjectIO.h"
#include "io/FileIO.h"
#include "model/BedHeightmap.h"
#include "model/Scene.h"
#include "parser/SrcParser.h"
#include "parser/GcodeParser.h"
#include "parser/DxfParser.h"
#include "render/AnimationRenderer.h"
#include "render/BedHeightmapRenderer.h"
#include "render/BedSettings.h"
#include "render/Camera.h"
#include "render/GeometryRenderer.h"
#include "render/PrintHeadRenderer.h"
#include "render/GizmoRenderer.h"
#include "render/GridRenderer.h"
#include "render/LightingSettings.h"
#include "render/LinkPreviewRenderer.h"
#include "render/StartPointRenderer.h"
#include "render/VertexRenderer.h"
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
    SceneObject object;
    if (ext == "dxf") {
        // DXF carries no header/footer/speed/tool-orientation of its own
        // -- built from nothing, same as a plain sliced .gcode import.
        // Use the Bed panel's Cell Template fix before exporting.
        object = parseDxfSplineLayers(name, lines, DxfImportOptions{});
    } else if (ext == "gcode" || ext == "nc") {
        object = parseGcode(name, lines);
    } else {
        object = parseSrc(name, lines);
    }
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
    LinkPreviewRenderer linkPreviewRenderer;
    StartPointRenderer startPointRenderer;
    VertexRenderer vertexRenderer;
    AnimationRenderer animationRenderer;
    PrintHeadRenderer printHeadRenderer;
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
    bedHeightmap.resize(bedHeightmap.cols, bedHeightmap.rows); // allocates elevationsMm to match the default cols/rows
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
        linkPreviewRenderer.rebuild(scene);
        startPointRenderer.rebuild(scene, bedSettings);
        vertexRenderer.rebuild(scene, renderSettings.showPrintPaths, renderSettings.showTravels);

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

        if (!scene.objects.empty()) {
            const StartPoint& sp = scene.objects.back().startPoint;
            if (sp.present) {
                std::printf("  start point (first safe position): srcLine=%d  A1=%.3f A2=%.3f A3=%.3f A4=%.3f A5=%.3f A6=%.3f\n",
                            sp.srcLine, sp.joints.a1, sp.joints.a2, sp.joints.a3, sp.joints.a4, sp.joints.a5, sp.joints.a6);
                if (sp.position.has_value()) {
                    std::printf("  start point display anchor (first Cartesian point): X %.2f  Y %.2f  Z %.2f\n",
                                sp.position->x, sp.position->y, sp.position->z);
                }
            } else {
                std::printf("  start point: none found (no joint-space PTP in this program)\n");
            }
        }

        // Mirror + interleave, exercised against the REAL file rather
        // than a hand-written snippet -- this is the feature the operator
        // reported as not working, and a synthetic 7-path sample doesn't
        // resemble a 24k-path production program at all.
        if (!scene.objects.empty()) {
            Scene mirrorCheck;
            SceneObject copyOfReal = scene.objects.back();
            copyOfReal.id = 0;
            int idA = mirrorCheck.addObject(std::move(copyOfReal)).id;

            auto worldBounds = [&](const SceneObject& o, double& minX, double& maxX) {
                minX = 1e30; maxX = -1e30;
                for (const auto& p : o.paths) {
                    minX = std::min({minX, applyTransform(o.transform, p.from).x, applyTransform(o.transform, p.to).x});
                    maxX = std::max({maxX, applyTransform(o.transform, p.from).x, applyTransform(o.transform, p.to).x});
                }
            };

            double aMinX, aMaxX;
            worldBounds(*mirrorCheck.findObject(idA), aMinX, aMaxX);

            SceneObject mirrored = mirrorObject(*mirrorCheck.findObject(idA), 200.0);
            int idB = mirrorCheck.addObject(std::move(mirrored)).id;
            double bMinX, bMaxX;
            worldBounds(*mirrorCheck.findObject(idB), bMinX, bMaxX);

            std::printf("  mirror check: source world X [%.1f .. %.1f], mirror world X [%.1f .. %.1f], gap=%.1fmm %s\n",
                        aMinX, aMaxX, bMinX, bMaxX, bMinX - aMaxX,
                        (bMinX >= aMaxX) ? "(clear)" : "(OVERLAP!)");

            InterleaveOptions opts;
            opts.detourMarginMm = 100.0;
            auto t6 = std::chrono::steady_clock::now();
            auto merged = buildInterleavedObject(mirrorCheck, {idA, idB}, opts);
            auto t7 = std::chrono::steady_clock::now();
            if (merged) {
                size_t printCount = 0, travelCount = 0;
                for (const auto& p : merged->paths) {
                    if (p.type == PathType::Print) ++printCount; else ++travelCount;
                }
                std::printf("  interleave check: %.0fms, %zu paths (%zu print, %zu travel), %zu layers, %zu source lines\n",
                            ms(t6, t7), merged->paths.size(), printCount, travelCount,
                            merged->layers.size(), merged->sourceLines.size());
                std::printf("  interleave check: layerActions carried = %zu (source had %zu)\n",
                            merged->layerActions.size(), mirrorCheck.findObject(idA)->layerActions.size());

                // The claim to actually verify: does the PRINT ORDER
                // alternate between parts layer by layer, or does it
                // finish one part then start the other? Walk the merged
                // paths in emission order and report which part each
                // printed segment belongs to (the two parts occupy
                // disjoint X ranges, so the X centre identifies them).
                double midX = (aMaxX + bMinX) * 0.5;
                std::string order;
                int shown = 0, lastLayer = -1;
                int runA = 0, runB = 0, maxRunA = 0, maxRunB = 0;
                for (const auto& p : merged->paths) {
                    if (p.type != PathType::Print || p.layer == lastLayer) continue;
                    lastLayer = p.layer;
                    bool isA = ((p.from.x + p.to.x) * 0.5) < midX;
                    if (isA) { ++runA; runB = 0; maxRunA = std::max(maxRunA, runA); }
                    else     { ++runB; runA = 0; maxRunB = std::max(maxRunB, runB); }
                    if (shown++ < 12) order += (isA ? 'A' : 'B');
                }
                std::printf("  interleave order: first 12 segments = %s  (longest same-part run: A=%d B=%d)\n",
                            order.c_str(), maxRunA, maxRunB);
                std::printf("  interleave order: %s\n",
                            (maxRunA <= 1 && maxRunB <= 1)
                                ? "ALTERNATING correctly (never two segments of the same part in a row)"
                                : "NOT alternating -- one part runs consecutively!");

                // The reported problem: cross-part travels must be FLAT.
                // Only SYNTHETIC cross-part transitions count here -- not
                // the source object's own header/footer travels (the
                // approach down to print start, the retreat away from it
                // before shutdown), which are genuine, deliberate, large
                // Z changes by design and are now also preserved as real
                // travel paths (see editor/Boilerplate.h).
                double maxTravelDz = 0.0;
                for (const auto& tp : merged->paths) {
                    if (tp.type != PathType::Travel) continue;
                    if (tp.srcLine < 0 || tp.srcLine >= static_cast<int>(merged->sourceLines.size())) continue;
                    const std::string& tl = merged->sourceLines[static_cast<size_t>(tp.srcLine)];
                    if (tl.find("GCODEFORGE INTERLEAVE TRAVEL") == std::string::npos &&
                        tl.find("GCODEFORGE in-layer reposition") == std::string::npos) continue;
                    maxTravelDz = std::max(maxTravelDz, std::abs(tp.to.z - tp.from.z));
                }
                // A layer-to-layer move MUST rise by one layer height --
                // that's not a lift, it's the print advancing. What must
                // NOT happen is a clearance hop far above the part. So
                // the meaningful check is "does any travel rise by more
                // than a single layer?", not "is any travel flat?".
                double layerStep = 0.0;
                {
                    const auto& L = mirrorCheck.findObject(idA)->layers;
                    if (L.size() >= 2) layerStep = std::abs(L[1].z - L[0].z);
                }
                std::printf("  interleave travels: largest travel Z change = %.3fmm (one layer = %.3fmm) %s\n",
                            maxTravelDz, layerStep,
                            (layerStep <= 0.0 || maxTravelDz <= layerStep + 1e-6)
                                ? "(no clearance hop -- only the layer step itself)"
                                : "(CLEARANCE HOP PRESENT!)");

                // The reported bug: exported speed is 0.
                ExportResult mergedExport;
                std::vector<std::string> mergedLines = buildExportedLines(*merged, mergedExport);
                int velCpCount = 0, zeroVelCpCount = 0;
                for (const auto& l : mergedLines) {
                    auto pos = l.find("$VEL.CP");
                    if (pos == std::string::npos) continue;
                    auto eq = l.find('=', pos);
                    if (eq == std::string::npos) continue;
                    ++velCpCount;
                    if (std::abs(std::stod(l.substr(eq + 1))) < 1e-9) ++zeroVelCpCount;
                }
                std::printf("  interleave speed: %d $VEL.CP command(s) in exported program, %d of them zero %s\n",
                            velCpCount, zeroVelCpCount,
                            (velCpCount > 0 && zeroVelCpCount == 0) ? "(GOOD)" : "(BUG!)");
            } else {
                std::printf("  interleave check: FAILED to build a merged object\n");
            }

            // Reported from real use: a 4-copy mirror+interleave export of
            // this exact file was rejected by the web editor's structural
            // validator (1630 CRITICAL issues) and its points failed to
            // load on the KUKA pendant -- traced to synthetic travel/
            // reposition lines missing A/B/C/E1-E6. 4 copies (not 2) is
            // required to reproduce it: the Y-detour path only triggers
            // going from the far part back past a middle one.
            Scene fourCopyCheck;
            SceneObject copyForFour = scene.objects.back();
            copyForFour.id = 0;
            int idFour = fourCopyCheck.addObject(std::move(copyForFour)).id;
            MirrorInterleaveOptions fourOpts;
            fourOpts.copies = 4;
            fourOpts.gapMm = 200.0;
            fourOpts.detourMarginMm = 100.0;
            auto merged4 = mirrorAndInterleave(fourCopyCheck, idFour, fourOpts);
            if (merged4) {
                ExportResult merged4Export;
                std::vector<std::string> merged4Lines = buildExportedLines(*merged4, merged4Export);
                int syntheticLines = 0, incompleteLines = 0;
                for (const auto& l : merged4Lines) {
                    if (l.find("GCODEFORGE INTERLEAVE TRAVEL") == std::string::npos &&
                        l.find("GCODEFORGE in-layer reposition") == std::string::npos) continue;
                    ++syntheticLines;
                    for (const char* field : {"A ", "B ", "C ", "E1 ", "E2 ", "E3 ", "E4 ", "E5 ", "E6 "}) {
                        if (l.find(field) == std::string::npos) { ++incompleteLines; break; }
                    }
                }
                std::printf("  4-copy interleave: %d synthetic travel/reposition lines, %d missing A/B/C/E1-E6 %s\n",
                            syntheticLines, incompleteLines,
                            (syntheticLines > 0 && incompleteLines == 0) ? "(GOOD)" : (syntheticLines == 0 ? "(no synthetic lines?!)" : "(BUG!)"));
            } else {
                std::printf("  4-copy interleave: FAILED to build a merged object\n");
            }

            // Reported from real use, with photos: a real Eidos file
            // mirrored 5 times and interleaved would not load on the
            // robot at all. Root cause: the merged program discarded
            // &ACCESS, safety interrupt declarations, BAS(#INITMOV,0),
            // the safe-pose PTP, and the shutdown block entirely.
            Scene fiveCopyCheck;
            SceneObject copyForFive = scene.objects.back();
            copyForFive.id = 0;
            int idFive = fiveCopyCheck.addObject(std::move(copyForFive)).id;
            MirrorInterleaveOptions fiveOpts;
            fiveOpts.copies = 5;
            fiveOpts.gapMm = 200.0;
            fiveOpts.detourMarginMm = 100.0;
            auto merged5 = mirrorAndInterleave(fiveCopyCheck, idFive, fiveOpts);
            if (merged5) {
                ExportResult merged5Export;
                std::vector<std::string> merged5Lines = buildExportedLines(*merged5, merged5Export);
                auto containsLine = [&](const std::string& needle) {
                    for (const auto& l : merged5Lines) if (l.find(needle) != std::string::npos) return true;
                    return false;
                };
                bool hasAccess = containsLine("&ACCESS");
                bool hasInterrupt = containsLine("INTERRUPT");
                bool hasSafePose = containsLine("PTP {A1");
                bool hasShutdown = containsLine("$OUT[7]=FALSE") || containsLine("$OUT[6]=FALSE") || containsLine("$OUT[5]=FALSE");
                bool endsWithEnd = false;
                for (auto it = merged5Lines.rbegin(); it != merged5Lines.rend(); ++it) {
                    if (it->empty()) continue; // real files can have a trailing blank line after END
                    endsWithEnd = (*it == "END");
                    break;
                }
                std::printf("  5-copy interleave: &ACCESS=%s INTERRUPT=%s safe-pose-PTP=%s shutdown-outs=%s ends-with-END=%s startPoint=%s %s\n",
                            hasAccess ? "yes" : "NO", hasInterrupt ? "yes" : "NO", hasSafePose ? "yes" : "NO",
                            hasShutdown ? "yes" : "NO", endsWithEnd ? "yes" : "NO",
                            merged5->startPoint.present ? "present" : "MISSING",
                            (hasAccess && hasInterrupt && hasSafePose && hasShutdown && endsWithEnd && merged5->startPoint.present)
                                ? "(GOOD)" : "(BUG!)");
                SceneObject reparsed5 = parseSrc("reparsed5", merged5Lines);
                std::printf("  5-copy interleave: model paths=%zu, re-parsed export paths=%zu %s\n",
                            merged5->paths.size(), reparsed5.paths.size(),
                            (reparsed5.paths.size() == merged5->paths.size()) ? "(match)" : "(MISMATCH!)");

                // The retreat travel's actual MOTION (not just the
                // shutdown text around it) must survive: the source
                // file's own retreat lifts to Z 508.00 before moving to
                // its far park/drip-avoidance position, and that lift
                // must come BEFORE the shutdown outputs fire.
                int liftIdx = -1, shutdownIdx = -1;
                for (int i = 0; i < static_cast<int>(merged5Lines.size()); ++i) {
                    if (liftIdx < 0 && merged5Lines[static_cast<size_t>(i)].find("Z 508.00") != std::string::npos) liftIdx = i;
                    if (shutdownIdx < 0 && merged5Lines[static_cast<size_t>(i)].find("$OUT[5]=FALSE") != std::string::npos) shutdownIdx = i;
                }
                std::printf("  5-copy interleave: retreat lift (Z 508.00) at line=%d, shutdown at line=%d %s\n",
                            liftIdx, shutdownIdx,
                            (liftIdx >= 0 && shutdownIdx >= 0 && liftIdx < shutdownIdx) ? "(GOOD)" : "(BUG! retreat motion missing or out of order)");

                ValidationReport merged5Report = validateForExport(*merged5, merged5Lines);
                std::printf("  5-copy interleave: pre-export validation: %d critical, %d warning %s\n",
                            merged5Report.criticalCount(), merged5Report.warningCount(),
                            merged5Report.issues.empty() ? "(GOOD)" : "(check issues below)");
                for (const auto& issue : merged5Report.issues) {
                    std::printf("    %s: %s\n", (issue.severity == ValidationSeverity::Critical) ? "CRITICAL" : "WARNING",
                                issue.message.c_str());
                }
            } else {
                std::printf("  5-copy interleave: FAILED to build a merged object\n");
            }
        }

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

            // Sanity check for the new pre-export validator: an untouched,
            // correctly-exported real production file must report ZERO
            // issues -- a validator that false-positives on a genuinely
            // correct file would just train the operator to click through
            // it, which defeats the point.
            ValidationReport realFileReport = validateForExport(scene.objects.back(), exportedLines);
            std::printf("Pre-export validation on the real file: %d critical, %d warning %s\n",
                        realFileReport.criticalCount(), realFileReport.warningCount(),
                        (realFileReport.issues.empty()) ? "(GOOD)" : "(check issues below)");
            for (const auto& issue : realFileReport.issues) {
                std::printf("  %s: %s\n", (issue.severity == ValidationSeverity::Critical) ? "CRITICAL" : "WARNING",
                            issue.message.c_str());
            }
        }

        // Print animation GL smoke test: there's no way to drive the
        // ImGui "Build simulation" button headlessly, so exercise the
        // same buildAnimationSequence() + AnimationRenderer::build()/
        // draw() + PrintHeadRenderer::rebuild()/draw() path directly here
        // against the real file, the only way to catch a GL-side crash
        // (bad attribute layout, etc.) before it ships.
        if (!scene.objects.empty()) {
            auto t8 = std::chrono::steady_clock::now();
            AnimationSequence animCheckSeq = buildAnimationSequence(scene.objects.back(), 5.0, 0.04, true, true);
            auto t9 = std::chrono::steady_clock::now();
            std::printf("Animation smoke test: build sequence %.1fms, %zu segment(s), totalDistance=%.1fmm, totalTime=%.1fs\n",
                        std::chrono::duration<double, std::milli>(t9 - t8).count(), animCheckSeq.segments.size(),
                        animCheckSeq.totalDistanceMm, animCheckSeq.totalTimeSeconds);

            animationRenderer.build(animCheckSeq, 7.0f, 3.0f, glm::vec3(0.85f, 0.55f, 0.15f), glm::vec3(0.35f, 0.55f, 0.85f));
            GLenum errAfterBuild = glGetError();
            std::printf("Animation smoke test: reveal mesh built, %zu triangle(s), glGetError=%u %s\n",
                        animationRenderer.triangleCount(), errAfterBuild, (errAfterBuild == GL_NO_ERROR) ? "(GOOD)" : "(BUG!)");

            // Draw at a few points along the timeline (start, mid, end) --
            // needs SOME view/projection matrix; identity is fine, this is
            // only checking for GL errors, not visual correctness.
            glm::mat4 dummyViewProj(1.0f);
            LightingSettings dummyLighting;
            for (double frac : {0.0, 0.5, 1.0}) {
                PlaybackState s = stateAtTime(animCheckSeq, animCheckSeq.totalTimeSeconds * frac);
                animationRenderer.draw(dummyViewProj, dummyLighting, static_cast<float>(s.coveredDistanceMm));
                printHeadRenderer.rebuild(glm::vec3(s.headPosition), 180.0f, 140.0f, 170.0f, 90.0f, 35.0f);
                printHeadRenderer.draw(dummyViewProj, dummyLighting);
            }
            GLenum errAfterDraw = glGetError();
            std::printf("Animation smoke test: drew reveal + print head at start/mid/end, glGetError=%u %s\n",
                        errAfterDraw, (errAfterDraw == GL_NO_ERROR) ? "(GOOD)" : "(BUG!)");
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
    bool tabWasDown = false;

    // Print animation playback state (editor/PrintAnimation.h). The
    // sequence is built once (on request, or when the active object
    // changes) and reused every frame; only animTimeSeconds changes
    // during normal playback, driven by wall-clock delta time * the
    // speed multiplier -- the exact same stateAtTime() call scrubbing
    // uses, so play and scrub can never disagree about where the head is
    // for a given time value.
    AnimationSequence animSequence;
    bool animBuilt = false;
    bool animRunning = false;
    double animTimeSeconds = 0.0;
    int animBuiltForObjectId = -1;
    double animLastFrameTime = 0.0;
    PlaybackState animState; // recomputed every frame from animTimeSeconds; read again at the draw call site below

    // Whatever path the cursor is currently over (not selected -- just
    // hovered), recomputed every frame for the status-bar readout.
    std::optional<PathRef> hoveredPath;

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

    // Move vs Rotate, toggled by R (edge-detected, like Tab). The gizmo
    // occupies this fraction of the viewport HEIGHT regardless of zoom or
    // distance -- see Camera::gizmoWorldRadius(). Shared by the move
    // arrows' length and the rotate ring's radius so switching modes
    // doesn't change the gizmo's apparent size.
    GizmoInteractionMode gizmoInteractionMode = GizmoInteractionMode::Move;
    bool rWasDown = false;
    constexpr float kGizmoScreenFraction = 0.15f;

    // Rotate-ring drag state. Like the move gizmo, everything needed to
    // compute a frame's result is captured ONCE at drag start and the
    // rotation is re-applied from that fixed snapshot every frame (not
    // accumulated), for the same reason closestPointOnAxisToRay requires
    // a fixed axisOrigin: incremental updates compound floating-point
    // drift and make each frame's delta relative to a different basis.
    bool gizmoRotateDragActive = false;
    GizmoTargetMode gizmoRotateDragMode = GizmoTargetMode::Object;
    glm::vec3 gizmoRotateDragPivot(0.0f);   // world space, fixed
    glm::vec2 gizmoRotateDragOriginScreen(0.0f);
    float gizmoRotateDragStartAngle = 0.0f; // radians
    Transform gizmoRotateDragStartTransform; // Object mode only
    std::vector<PathDragSnapshot> gizmoRotateDragPathSnapshots; // Start/End/Whole only

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
        if (editorUi.saveProjectRequested()) {
            editorUi.clearSaveProjectRequest();
            if (auto path = showSaveProjectDialog(window)) {
                ProjectData project;
                project.scene = scene;
                project.bed = bedSettings;
                project.heightmap = bedHeightmap;
                project.lighting = lightingSettings;
                project.render = renderSettings;
                project.colorMode = colorMode;
                if (saveProject(*path, project)) {
                    std::printf("Saved project: %s (%zu object(s))\n", path->c_str(), scene.objects.size());
                } else {
                    std::fprintf(stderr, "Could not save project to: %s\n", path->c_str());
                }
            }
        }
        if (editorUi.loadProjectRequested()) {
            editorUi.clearLoadProjectRequest();
            if (auto path = showOpenProjectDialog(window)) {
                ProjectData project;
                if (loadProject(*path, project)) {
                    // loadProject only commits on success, so a bad file
                    // leaves the current session untouched rather than
                    // half-replacing it.
                    scene = std::move(project.scene);
                    bedSettings = project.bed;
                    bedHeightmap = project.heightmap;
                    lightingSettings = project.lighting;
                    renderSettings = project.render;
                    colorMode = project.colorMode;
                    undoStack = UndoStack{}; // history refers to the OLD scene; keeping it would let undo restore a foreign session
                    sceneDirty = true;
                    bedDirty = true;
                    std::printf("Loaded project: %s (%zu object(s))\n", path->c_str(), scene.objects.size());
                } else {
                    std::fprintf(stderr, "Could not load project from: %s\n", path->c_str());
                }
            }
        }
        // The Export SRC dialog (editor/ExportValidation.h's individually-
        // runnable checks, see EditorUI::drawExportDialog) decides WHETHER
        // and WITH WHAT OPTIONS to save; this is the only place that
        // actually touches disk, since it's the only place with a
        // GLFWwindow* for the native save dialog.
        if (editorUi.exportSaveRequested()) {
            editorUi.clearExportSaveRequest();
            if (SceneObject* active = scene.activeObject()) {
                if (auto path = showSaveSrcDialog(window, active->name + ".src")) {
                    ExportOptions options;
                    options.roundSpeedsTo4Decimals = editorUi.exportRoundSpeedsTo4Decimals();
                    ExportResult exportResult = exportSrcToFile(*active, *path, options);
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

        // Print animation (editor/PrintAnimation.h). Build/rebuild only
        // on request or when the active object changes underneath a
        // stale sequence -- never every frame, since it walks and
        // subdivides every path. Play/pause/stop/scrub all funnel down
        // to the ONE shared stateAtTime() call, so the timeline slider
        // and real-time playback can never show a different head
        // position for the same time value.
        {
            EditorUI::AnimationSettings& animSettings = editorUi.animationSettings();
            SceneObject* animActive = scene.activeObject();

            if (editorUi.animationBuildRequested()) {
                editorUi.clearAnimationBuildRequest();
                if (animActive) {
                    animSequence = buildAnimationSequence(*animActive, animSettings.maxSegmentLengthMm,
                                                           animSettings.fallbackSpeedMps,
                                                           animSettings.includePrint, animSettings.includeTravel);
                    animationRenderer.build(animSequence, renderSettings.beadWidthMm, renderSettings.beadHeightMm,
                                             glm::vec3(0.85f, 0.55f, 0.15f), glm::vec3(0.35f, 0.55f, 0.85f));
                    animBuilt = !animSequence.segments.empty();
                    animBuiltForObjectId = animActive->id;
                    animTimeSeconds = 0.0;
                    animRunning = false;
                }
            }
            // A stale sequence (built for an object that's since been
            // deleted, or a different object made active) shouldn't keep
            // claiming to be "built" -- next Play/scrub would silently
            // simulate the wrong thing.
            if (animBuilt && (!animActive || animActive->id != animBuiltForObjectId)) {
                animBuilt = false;
                animRunning = false;
            }

            if (editorUi.animationPlayRequested()) {
                editorUi.clearAnimationPlayRequest();
                if (animBuilt) {
                    if (animTimeSeconds >= animSequence.totalTimeSeconds - 1e-9) animTimeSeconds = 0.0; // restart if it already finished
                    animRunning = true;
                    animLastFrameTime = glfwGetTime();
                }
            }
            if (editorUi.animationPauseRequested()) {
                editorUi.clearAnimationPauseRequest();
                animRunning = false;
            }
            if (editorUi.animationStopRequested()) {
                editorUi.clearAnimationStopRequest();
                animRunning = false;
                animTimeSeconds = 0.0;
            }
            if (editorUi.animationExitRequested()) {
                editorUi.clearAnimationExitRequest();
                animBuilt = false;
                animRunning = false;
                animTimeSeconds = 0.0;
            }
            if (editorUi.animationScrubbed()) {
                animTimeSeconds = editorUi.animationScrubTimeSeconds();
                animRunning = false; // scrubbing pauses -- matches every other timeline UI
                editorUi.clearAnimationScrub();
            }

            if (animRunning && animBuilt) {
                double now = glfwGetTime();
                double delta = std::max(0.0, std::min(0.1, now - animLastFrameTime)); // cap a stall/breakpoint from causing a huge jump
                animLastFrameTime = now;
                animTimeSeconds += delta * static_cast<double>(animSettings.speedMultiplier);
                if (animTimeSeconds >= animSequence.totalTimeSeconds) {
                    animTimeSeconds = animSequence.totalTimeSeconds;
                    animRunning = false;
                }
            }

            animState = animBuilt ? stateAtTime(animSequence, animTimeSeconds) : PlaybackState{};
            if (animBuilt && animSettings.showHead && !animState.finished) {
                printHeadRenderer.rebuild(glm::vec3(animState.headPosition), animSettings.headWidthMm,
                                           animSettings.headDepthMm, animSettings.headHeightMm,
                                           animSettings.nozzleLengthMm, animSettings.nozzleWidthMm);
            }
            editorUi.setAnimationReadout(animTimeSeconds, animSequence.totalTimeSeconds, animRunning,
                                          animState.finished, animBuilt);
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

        // Hover readout: pick (without selecting) whatever path is under
        // the cursor, so the status bar can report its layer and speed.
        // Reuses the exact same picking call a click would make, so what
        // the readout names is guaranteed to be what a click would grab --
        // a separate "close enough to hover" rule would eventually
        // disagree with the click and be worse than no readout at all.
        hoveredPath.reset();
        if (viewportInputActive && !altHeld && height > 0) {
            ScreenProjector hoverProjector{viewProj, static_cast<float>(width), static_cast<float>(height)};
            glm::vec2 cursor(static_cast<float>(cursorX), static_cast<float>(cursorY));
            hoveredPath = pickNearestPath(scene, hoverProjector, cursor, kClickPickRadiusPixels,
                                           renderSettings.selectBackfacing);
        }

        // Resolve the hovered PathRef into displayable values here (where
        // the Scene is in hand) rather than handing EditorUI a raw ref and
        // making it do scene lookups. Set now, drawn next frame -- a
        // one-frame lag nobody can perceive on a hover readout.
        EditorUI::HoverInfo hoverInfo;
        if (hoveredPath) {
            if (const SceneObject* object = scene.findObject(hoveredPath->objectId)) {
                for (const auto& p : object->paths) {
                    if (p.number != hoveredPath->pathNumber) continue;
                    hoverInfo.valid = true;
                    hoverInfo.objectName = object->name;
                    hoverInfo.pathNumber = p.number;
                    hoverInfo.layer = p.layer;
                    hoverInfo.speed = p.effectiveSpeed();
                    hoverInfo.isTravel = (p.type == PathType::Travel);
                    break;
                }
            }
        }
        editorUi.setHoverInfo(hoverInfo);

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
            gizmoRotateDragActive = false;
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

            // The move/rotate gizmo takes priority over path selection: if
            // the click lands on it, drag the object/paths; only fall
            // through to click/marquee-select when it doesn't.
            if (leftPressed && !leftWasPressed) {
                bool gizmoHit = false;
                if (active) {
                    if (auto origin = computeGizmoOrigin(*active, effectiveMode)) {
                        ScreenProjector projector{viewProj, static_cast<float>(width), static_cast<float>(height)};
                        // Same world-size formula the renderer uses (see
                        // the draw-side rebuild() call below) -- picking
                        // geometry that doesn't match rendered geometry
                        // would mean clicking exactly on the visible
                        // gizmo sometimes misses it.
                        float gizmoSize = camera.gizmoWorldRadius(*origin, kGizmoScreenFraction);

                        if (gizmoInteractionMode == GizmoInteractionMode::Move) {
                            std::vector<GizmoAxisScreenSegment> segments;
                            for (GizmoAxis axis : {GizmoAxis::X, GizmoAxis::Y, GizmoAxis::Z}) {
                                glm::vec3 tip = *origin + gizmoAxisDirection(axis) * gizmoSize;
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
                        } else { // Rotate
                            if (auto originScreen = projector.project(*origin)) {
                                float screenRadiusPixels = kGizmoScreenFraction * static_cast<float>(height) * 0.5f;
                                if (pickGizmoRing(*originScreen, screenRadiusPixels, current, kGizmoPickRadiusPixels)) {
                                    gizmoRotateDragActive = true;
                                    gizmoRotateDragMode = effectiveMode;
                                    gizmoRotateDragPivot = *origin;
                                    gizmoRotateDragOriginScreen = *originScreen;
                                    gizmoRotateDragStartAngle = angleAroundScreenPoint(*originScreen, current);
                                    if (effectiveMode == GizmoTargetMode::Object) {
                                        gizmoRotateDragStartTransform = active->transform;
                                    } else {
                                        gizmoRotateDragPathSnapshots = buildDragSnapshots(*active, effectiveMode);
                                    }
                                    undoStack.beginContinuousEdit(scene);
                                    gizmoHit = true;
                                }
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
                } else if (gizmoRotateDragActive && active) {
                    float currentAngle = angleAroundScreenPoint(gizmoRotateDragOriginScreen, current);
                    double deltaDegrees = (currentAngle - gizmoRotateDragStartAngle) * (180.0 / 3.14159265358979323846);

                    if (gizmoRotateDragMode == GizmoTargetMode::Object) {
                        // Reset-then-apply from the fixed start snapshot,
                        // same reasoning as the move gizmo's
                        // gizmoDragStartValue: re-deriving from a fixed
                        // basis every frame avoids compounding drift.
                        active->transform = gizmoRotateDragStartTransform;
                        rotateObjectAroundPivot(active->transform, glm::dvec3(gizmoRotateDragPivot), deltaDegrees);
                    } else {
                        // Rotate each affected endpoint (selected paths
                        // AND any connected unselected neighbor, same set
                        // the move gizmo uses) around the fixed WORLD
                        // pivot, converting through the object's
                        // transform on the way in and out since
                        // Path::from/to are stored in local space.
                        for (const auto& snap : gizmoRotateDragPathSnapshots) {
                            Path* path = active->findPath(snap.pathNumber);
                            if (!path) continue;
                            if (snap.moveFrom) {
                                glm::dvec3 world = applyTransform(active->transform, snap.startFrom);
                                glm::dvec3 rotated = rotatePointAroundPivotZ(world, glm::dvec3(gizmoRotateDragPivot), deltaDegrees);
                                path->from = inverseApplyTransform(active->transform, rotated);
                            }
                            if (snap.moveTo) {
                                glm::dvec3 world = applyTransform(active->transform, snap.startTo);
                                glm::dvec3 rotated = rotatePointAroundPivotZ(world, glm::dvec3(gizmoRotateDragPivot), deltaDegrees);
                                path->to = inverseApplyTransform(active->transform, rotated);
                            }
                        }
                    }
                    sceneDirty = true;
                } else if (glm::length(current - mouseDownPos) > kDragThresholdPixels) {
                    isDraggingMarquee = true;
                }
            } else if (!leftPressed && leftWasPressed) {
                if (gizmoDragAxis || gizmoRotateDragActive) {
                    undoStack.commitContinuousEdit();
                    gizmoDragAxis.reset();
                    gizmoDragPathSnapshots.clear();
                    gizmoRotateDragActive = false;
                    gizmoRotateDragPathSnapshots.clear();
                } else if (editorUi.heightmapPaintModeActive() && !isDraggingMarquee) {
                    // Paint mode intercepts a plain click BEFORE normal
                    // path selection -- exactly the nearest heightmap
                    // vertex to the click, no radius/falloff, nudged by
                    // +/-Power depending on Add/Remove (see
                    // EditorUI::drawBedPanel's heightmap paint controls).
                    ScreenProjector projector{viewProj, static_cast<float>(width), static_cast<float>(height)};
                    if (auto vertex = pickNearestHeightmapVertex(bedHeightmap, bedSettings, projector, current,
                                                                  kClickPickRadiusPixels)) {
                        float sign = (editorUi.heightmapPaintTool() == EditorUI::HeightmapPaintTool::Add) ? 1.0f : -1.0f;
                        bedHeightmap.at(vertex->col, vertex->row) += sign * editorUi.heightmapPaintPowerMm();
                        bedDirty = true;
                    }
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

            // Tab toggles all panels. Edge-detected (fires once per
            // press, not once per frame held) like the undo/redo keys.
            // Guarded by WantCaptureKeyboard so it can't fire while
            // ImGui is using Tab to move between text fields.
            bool tabDown = glfwGetKey(window, GLFW_KEY_TAB) == GLFW_PRESS;
            if (tabDown && !tabWasDown) editorUi.togglePanels();
            tabWasDown = tabDown;

            // R switches the gizmo between Move and Rotate. Doesn't fire
            // mid-drag (a drag in progress finishes in whichever mode it
            // started in -- switching mode under an active drag would
            // mean the mouse-up code no longer matches what mouse-down
            // armed).
            bool rDown = glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS;
            if (rDown && !rWasDown && !gizmoDragAxis && !gizmoRotateDragActive) {
                gizmoInteractionMode = (gizmoInteractionMode == GizmoInteractionMode::Move)
                                           ? GizmoInteractionMode::Rotate
                                           : GizmoInteractionMode::Move;
            }
            rWasDown = rDown;

            ctrlZWasDown = ctrlHeld && zDown;
            ctrlYWasDown = ctrlHeld && yDown;
        }

        if (sceneDirty) {
            // Only rebuild whichever renderer is actually on screen -- no
            // reason to spend CPU building bead geometry nobody is looking
            // at, or vice versa. Matters once files get big (docs/PLAN.md
            // milestone 11 is the deeper version of this idea).
            if (renderSettings.mode == RenderMode::Lines) {
                sceneRenderer.rebuild(scene, colorMode, renderSettings.showPrintPaths, renderSettings.showTravels);
            } else {
                geometryRenderer.rebuild(scene, colorMode, renderSettings.beadWidthMm, renderSettings.beadHeightMm,
                                          renderSettings.showPrintPaths, renderSettings.showTravels);
            }
            linkPreviewRenderer.rebuild(scene); // cheap; objects/links may have changed regardless of which mode's mesh needed rebuilding
            startPointRenderer.rebuild(scene, bedSettings);
            vertexRenderer.rebuild(scene, renderSettings.showPrintPaths, renderSettings.showTravels);
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
                geometryRenderer.rebuild(scene, colorMode, renderSettings.beadWidthMm, renderSettings.beadHeightMm,
                                          renderSettings.showPrintPaths, renderSettings.showTravels);
            }
        }
        if (bedDirty) {
            grid.rebuild(bedSettings);
            // Reused for heightmap edits too (columns/rows, per-point
            // values, visibility), not just bed size/position -- cols/rows
            // are independent of bed size now (the operator sets them
            // directly), so this is just a mesh rebuild, no resize needed.
            bedHeightmapRenderer.rebuild(bedSettings, bedHeightmap);
        }

        glClearColor(0.09f, 0.10f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (height > 0) {
            if (g_showGrid) grid.draw(viewProj);
            if (bedHeightmap.visible) bedHeightmapRenderer.draw(viewProj, lightingSettings);
            linkPreviewRenderer.draw(viewProj); // pending (not-yet-baked) object links, drawn regardless of Lines/Geometry mode
            if (renderSettings.showStartPoint) startPointRenderer.draw(viewProj);
            if (renderSettings.showVertices) vertexRenderer.draw(viewProj, renderSettings.vertexSizePixels);
            // Selection highlight draws BEFORE the real geometry, wide and
            // depth-tested normally -- the real geometry (always at least
            // as close to the camera as its own centerline) naturally
            // overwrites the highlight's color across its footprint,
            // leaving only the wide highlight's edges visible as an
            // outline/border. See SelectionHighlightRenderer.h.
            selectionHighlight.draw(viewProj);
            // While a print simulation is built, its own progressively-
            // revealed mesh replaces the normal always-fully-drawn scene
            // geometry entirely -- showing both at once would defeat the
            // reveal (the "already printed" object sitting underneath,
            // fully visible, the whole time). Known simplification: this
            // hides ALL objects, not just the one being animated -- fine
            // for the common single-object workflow, a rough edge in a
            // multi-object scene.
            if (animBuilt) {
                animationRenderer.draw(viewProj, lightingSettings, static_cast<float>(animState.coveredDistanceMm));
                if (editorUi.animationSettings().showHead && !animState.finished) printHeadRenderer.draw(viewProj, lightingSettings);
            } else if (renderSettings.mode == RenderMode::Lines) {
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
                    float gizmoSize = camera.gizmoWorldRadius(*origin, kGizmoScreenFraction);
                    gizmoRenderer.rebuild(*origin, gizmoSize, gizmoInteractionMode); // cheap -- rebuilding every frame is fine
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
