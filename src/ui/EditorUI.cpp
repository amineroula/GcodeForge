#include "ui/EditorUI.h"
#include "editor/BedConform.h"
#include "editor/InterleavePrint.h"
#include "editor/MirrorObject.h"
#include "editor/ObjectLinking.h"
#include "editor/PathSplit.h"
#include "editor/RotatePaths.h"
#include "editor/Selection.h"
#include "editor/SpeedEditing.h"

#include <imgui.h>
#include <algorithm>
#include <cstdio>

namespace {

// Reads Shift/Ctrl off ImGui's IO to decide how a selection action combines
// with the existing selection -- click=Replace, shift-click=Add,
// ctrl-click=Subtract. Matches the original's selection semantics
// (docs/PLAN.md task 7).
SelectionCompose currentSelectionCompose() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl) return SelectionCompose::Subtract;
    if (io.KeyShift) return SelectionCompose::Add;
    return SelectionCompose::Replace;
}

const char* colorModeLabel(ColorMode mode) {
    switch (mode) {
        case ColorMode::Object: return "Object";
        case ColorMode::Type: return "Type (print/travel)";
        case ColorMode::Layer: return "Layer";
        case ColorMode::Group: return "Selection group";
        case ColorMode::Speed: return "Speed";
        case ColorMode::Sequence: return "Print order (blue=first, red=last)";
    }
    return "?";
}

} // namespace

void EditorUI::sectionLabel(const char* text) {
    if (boldFont_) ImGui::PushFont(boldFont_);
    ImGui::TextUnformatted(text);
    if (boldFont_) ImGui::PopFont();
}

void EditorUI::draw(Scene& scene, ColorMode& colorMode, Camera& camera, RenderSettings& renderSettings,
                     BedSettings& bedSettings, LightingSettings& lightingSettings, BedHeightmap& bedHeightmap,
                     UndoStack& undoStack, size_t renderedPrimitiveCount, bool& sceneDirty, bool& selectionDirty, bool& bedDirty) {
    drawMenuBar(scene, undoStack, sceneDirty);
    drawStatusBar();

    // Panels collapsed: skip both floating windows entirely, leaving an
    // unobstructed view of the viewport. The menu bar and status bar stay
    // visible either way -- the toggle button lives in the menu bar, so
    // it's always reachable to bring the panels back, and the hover
    // readout is most useful precisely when the panels are out of the way.
    if (!panelsVisible_) return;

    ImGui::SetNextWindowPos(ImVec2(12, 32), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(380, 680), ImGuiCond_FirstUseEver);
    ImGui::Begin("Editor");

    // Three tabs instead of one long scroll. The panel had grown to ten
    // stacked sections -- View, Objects, Mirror, Transform, Layers, Layer
    // actions, Groups, Speed, Bed Conform, Colour, Stats -- which is how
    // a whole feature ("Mirror the object") ended up invisible: it was
    // just another collapsed bar somewhere in the middle. Grouping by
    // WHAT YOU'RE ACTING ON (the view / the whole scene / the selected
    // object) means each tab is short enough to take in at a glance.
    SceneObject* active = scene.activeObject();
    if (ImGui::BeginTabBar("editorTabs")) {

        if (ImGui::BeginTabItem("View")) {
            drawViewPanel(camera, renderSettings, sceneDirty);
            ImGui::Separator();
            drawColorModePanel(colorMode, sceneDirty);
            ImGui::Separator();
            drawStatsPanel(scene, renderSettings.mode, renderedPrimitiveCount);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Scene")) {
            drawObjectListPanel(scene, undoStack, sceneDirty);
            drawMultiPartPanel(scene, undoStack, sceneDirty);
            ImGui::EndTabItem();
        }

        // Labelled with the active object's name rather than a generic
        // "Object", so it's obvious WHICH object these controls edit --
        // with several mirrored copies in the list that stops being
        // guessable.
        std::string objectTabLabel = active ? ("Object: " + active->name + "###objtab")
                                            : std::string("Object###objtab");
        if (ImGui::BeginTabItem(objectTabLabel.c_str())) {
            if (active) {
                drawTransformPanel(scene, *active, undoStack, sceneDirty);
                drawLayerTablePanel(scene, *active, undoStack, sceneDirty, selectionDirty);
                drawSelectionGroupPanel(scene, *active, undoStack, sceneDirty, selectionDirty);
                drawSpeedPanel(scene, *active, undoStack, sceneDirty);
                drawBedConformPanel(scene, *active, bedHeightmap, bedSettings, undoStack, sceneDirty);
            } else {
                ImGui::TextDisabled("No object loaded. File > Open to load a .src file.");
            }
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();

    float displayWidth = ImGui::GetIO().DisplaySize.x;
    ImGui::SetNextWindowPos(ImVec2(displayWidth - 332, 32), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(320, 320), ImGuiCond_FirstUseEver);
    ImGui::Begin("Bed");
    drawBedPanel(bedSettings, lightingSettings, bedHeightmap, bedDirty);
    ImGui::End();
}

void EditorUI::drawMenuBar(Scene& scene, UndoStack& undoStack, bool& sceneDirty) {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open SRC / G-code...")) {
                openFileRequested_ = true;
            }
            bool hasActive = (scene.activeObject() != nullptr);
            if (ImGui::MenuItem("Save SRC As...", nullptr, false, hasActive)) {
                saveSrcRequested_ = true;
            }
            ImGui::Separator();
            // A .src is a robot program and can only hold what the robot
            // needs. Selections, groups, bed + heightmap, the measured
            // safe point, object links, per-object colours -- none of
            // that has anywhere to live in a .src, so it dies with the
            // app unless it goes in a project file.
            if (ImGui::MenuItem("Open Project...")) loadProjectRequested_ = true;
            if (ImGui::MenuItem("Save Project As...")) saveProjectRequested_ = true;
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, undoStack.canUndo())) {
                undoStack.undo(scene);
                sceneDirty = true;
            }
            if (ImGui::MenuItem("Redo", "Ctrl+Y", false, undoStack.canRedo())) {
                undoStack.redo(scene);
                sceneDirty = true;
            }
            ImGui::EndMenu();
        }
        // Panel collapse toggle: lives directly in the menu bar (not its
        // own floating window) so it's always visible and reachable
        // regardless of panelsVisible_'s current state -- clicking it
        // hides/shows the Editor and Bed windows for an unobstructed view
        // of the viewport, click again to bring them back.
        if (ImGui::SmallButton(panelsVisible_ ? "Hide panels" : "Show panels")) {
            panelsVisible_ = !panelsVisible_;
        }
        ImGui::EndMainMenuBar();
    }
}

void EditorUI::drawStatusBar() {
    // A real bottom-of-screen status strip, not a floating window: fixed
    // to the full display width at the bottom, no decoration, no input
    // capture -- so it never steals a click meant for the viewport
    // underneath it.
    const float kHeight = 28.0f;
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0.0f, io.DisplaySize.y - kHeight));
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, kHeight));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                              ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                              ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 5.0f));
    if (ImGui::Begin("##statusbar", nullptr, flags)) {
        if (hoverInfo_.valid) {
            if (hoverInfo_.isTravel) {
                ImGui::Text("%s   path %d   TRAVEL   speed %.4f",
                            hoverInfo_.objectName.c_str(), hoverInfo_.pathNumber, hoverInfo_.speed);
            } else {
                ImGui::Text("%s   path %d   layer %d   speed %.4f",
                            hoverInfo_.objectName.c_str(), hoverInfo_.pathNumber,
                            hoverInfo_.layer, hoverInfo_.speed);
            }
        } else {
            ImGui::TextDisabled("Hover a path to see its layer and speed.   Tab: hide/show panels");
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

void EditorUI::drawViewPanel(Camera& camera, RenderSettings& renderSettings, bool& dirty) {
    sectionLabel("View");

    bool isPerspective = (camera.projection() == Camera::Projection::Perspective);
    if (ImGui::RadioButton("Perspective", isPerspective)) camera.setProjection(Camera::Projection::Perspective);
    ImGui::SameLine();
    if (ImGui::RadioButton("Orthographic", !isPerspective)) camera.setProjection(Camera::Projection::Orthographic);

    if (ImGui::Button("Top")) camera.setPreset(Camera::Preset::Top);
    ImGui::SameLine();
    if (ImGui::Button("Front")) camera.setPreset(Camera::Preset::Front);
    ImGui::SameLine();
    if (ImGui::Button("Right")) camera.setPreset(Camera::Preset::Right);
    ImGui::SameLine();
    if (ImGui::Button("Iso")) camera.setPreset(Camera::Preset::Iso);

    ImGui::TextDisabled("Alt+drag: LMB orbit / MMB pan / RMB zoom. Scroll also zooms.");
    ImGui::TextDisabled("Plain click: select path. Plain drag: marquee-select.");

    ImGui::Spacing();
    sectionLabel("Display");
    // Pure display filters -- hiding a category doesn't delete it or
    // exclude it from export, it just declutters the viewport.
    if (ImGui::Checkbox("Paths", &renderSettings.showPrintPaths)) dirty = true;
    ImGui::SameLine();
    if (ImGui::Checkbox("Travels", &renderSettings.showTravels)) dirty = true;
    ImGui::SameLine();
    ImGui::Checkbox("Start point", &renderSettings.showStartPoint); // draw-time only, no rebuild needed
    if (ImGui::Checkbox("Vertices", &renderSettings.showVertices)) dirty = true;
    if (renderSettings.showVertices) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110.0f);
        ImGui::DragFloat("Size##vtx", &renderSettings.vertexSizePixels, 0.2f, 1.0f, 20.0f, "%.0f px");
    }
    ImGui::TextDisabled("Start point = the joint-space PTP the robot moves to before printing.");
    ImGui::TextDisabled("Vertices = the actual endpoints of each motion command.");

    ImGui::Spacing();
    sectionLabel("Render mode");
    bool isLines = (renderSettings.mode == RenderMode::Lines);
    if (ImGui::RadioButton("Lines", isLines)) {
        if (!isLines) { renderSettings.mode = RenderMode::Lines; dirty = true; }
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Geometry (bead)", !isLines)) {
        if (isLines) { renderSettings.mode = RenderMode::Geometry; dirty = true; }
    }

    if (renderSettings.mode == RenderMode::Geometry) {
        // DragFloat instead of SliderFloat: click-drag the number itself to
        // scrub the value (also still double-click/ctrl-click to type an
        // exact number), rather than needing to hit a thin slider track.
        if (ImGui::DragFloat("Bead width (mm)", &renderSettings.beadWidthMm, 0.1f, 0.5f, 50.0f, "%.1f")) dirty = true;
        if (ImGui::DragFloat("Bead height (mm)", &renderSettings.beadHeightMm, 0.1f, 0.5f, 50.0f, "%.1f")) dirty = true;
        ImGui::TextDisabled("Print paths render as mitered solid bead tubes; travel paths stay as thin lines.");
        // Rendering-only toggle -- no rebuild needed, just checked at draw time.
        ImGui::Checkbox("Backface culling (hide inside of tubes)", &renderSettings.backfaceCulling);

        // Selection style is a pure draw-time choice too -- no rebuild
        // needed, GeometryRenderer already builds both the outline mesh
        // and the per-vertex selected flag every rebuild regardless of
        // which style is currently active, so switching is instant.
        static const char* kSelectionStyleNames[] = {
            "Outline", "Pulse (glow selected, dim rest)", "Stripes (moving hazard tape)", "Wireframe cage"
        };
        int styleIndex = static_cast<int>(renderSettings.selectionStyle);
        if (ImGui::Combo("Selection style", &styleIndex, kSelectionStyleNames, 4)) {
            renderSettings.selectionStyle = static_cast<SelectionStyle>(styleIndex);
        }
    }

    ImGui::Spacing();
    // Picking-only toggle -- affects the next click, nothing to rebuild.
    ImGui::Checkbox("Select backfacing/hidden geometry", &renderSettings.selectBackfacing);
    ImGui::TextDisabled("Off: clicking prefers the path nearest the camera. On: prefers whichever is closest on screen.");

    ImGui::Spacing();
    sectionLabel("Move gizmo");
    GizmoTargetMode& gm = renderSettings.gizmoMode;
    if (ImGui::RadioButton("Object", gm == GizmoTargetMode::Object)) gm = GizmoTargetMode::Object;
    ImGui::SameLine();
    if (ImGui::RadioButton("Start", gm == GizmoTargetMode::Start)) gm = GizmoTargetMode::Start;
    ImGui::SameLine();
    if (ImGui::RadioButton("End", gm == GizmoTargetMode::End)) gm = GizmoTargetMode::End;
    ImGui::SameLine();
    if (ImGui::RadioButton("Whole", gm == GizmoTargetMode::Whole)) gm = GizmoTargetMode::Whole;
    ImGui::TextDisabled("Object moves the whole object. Start/End/Whole edit the CURRENT PATH SELECTION directly");
    ImGui::TextDisabled("(with no selection, these fall back to Object mode). Start/End can break connectivity with");
    ImGui::TextDisabled("an unselected neighbor; Whole translates each selected path rigidly, so it never does.");
    ImGui::TextDisabled("Press R in the viewport to switch the gizmo between Move (arrows) and Rotate (ring) --");
    ImGui::TextDisabled("rotation always spins around the same Object/Start/End/Whole target set above.");
}

void EditorUI::drawBedPanel(BedSettings& bed, LightingSettings& lighting, BedHeightmap& heightmap, bool& bedDirty) {
    sectionLabel("Bed size");
    if (ImGui::InputFloat("Width (mm)", &bed.widthMm, 10.0f, 100.0f, "%.0f")) { bed.widthMm = std::max(bed.widthMm, 10.0f); bedDirty = true; }
    if (ImGui::InputFloat("Depth (mm)", &bed.depthMm, 10.0f, 100.0f, "%.0f")) { bed.depthMm = std::max(bed.depthMm, 10.0f); bedDirty = true; }

    ImGui::Spacing();
    sectionLabel("Bed position (movement)");
    if (ImGui::InputFloat("Origin X (mm)", &bed.originXMm, 10.0f, 100.0f, "%.1f")) bedDirty = true;
    if (ImGui::InputFloat("Origin Y (mm)", &bed.originYMm, 10.0f, 100.0f, "%.1f")) bedDirty = true;
    if (ImGui::InputFloat("Origin Z (mm)", &bed.originZMm, 10.0f, 100.0f, "%.1f")) bedDirty = true;

    ImGui::Spacing();
    sectionLabel("Grid");
    if (ImGui::Checkbox("Show grid", &bed.showGrid)) bedDirty = true;
    if (ImGui::InputFloat("Line spacing (mm)", &bed.gridSpacingMm, 5.0f, 50.0f, "%.0f")) {
        bed.gridSpacingMm = std::max(bed.gridSpacingMm, 1.0f);
        bedDirty = true;
    }
    ImGui::TextDisabled("Default: 100mm (10cm) per line.");

    if (ImGui::Button("Reset bed")) {
        bed = BedSettings{};
        bedDirty = true;
    }

    // Safe point: a CELL property, which is why it lives here with the
    // bed rather than on the part. The same robot goes to the same safe
    // pose for every job, so it's entered once and saved with the bed.
    ImGui::Spacing();
    sectionLabel("Robot safe point (start position)");
    ImGui::TextWrapped("The joint-space PTP the robot moves to before printing has no X/Y/Z in the "
                        "program -- read it off the pendant (Display > Actual Position > Cartesian, "
                        "with Tool 1 / Base 1 active) and enter it here.");

    float safeXYZ[3] = {bed.safePointXMm, bed.safePointYMm, bed.safePointZMm};
    if (ImGui::DragFloat3("Safe X/Y/Z (mm)", safeXYZ, 1.0f, 0.0f, 0.0f, "%.1f")) {
        bed.safePointXMm = safeXYZ[0];
        bed.safePointYMm = safeXYZ[1];
        bed.safePointZMm = safeXYZ[2];
        bed.safePointMeasured = true;
        bedDirty = true;
    }
    if (bed.safePointMeasured) {
        ImGui::TextColored(ImVec4(0.45f, 0.90f, 0.50f, 1.0f), "Measured -- marker shows the real position.");
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear##safepoint")) {
            bed.safePointMeasured = false;
            bedDirty = true;
        }
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.70f, 0.30f, 1.0f),
                            "Not measured -- marker falls back to the program's first point, which is NOT the safe pose.");
    }

    ImGui::Spacing();
    if (ImGui::Button("Save Bed...")) saveBedRequested_ = true;
    ImGui::SameLine();
    if (ImGui::Button("Load Bed...")) loadBedRequested_ = true;
    ImGui::TextDisabled("Bed file stores size, origin, grid, heightmap, and the safe point.");

    // Lighting affects only Geometry-mode shading (a per-frame shader
    // uniform, not baked into any mesh) -- no bedDirty/sceneDirty needed,
    // the next frame's draw call just picks up the new values directly.
    ImGui::Spacing();
    ImGui::Spacing();
    sectionLabel("Environment / Lighting");
    ImGui::TextDisabled("Affects Geometry view mode shading only.");

    if (ImGui::Button("Three-point lighting preset")) {
        // Classic film/photography three-point setup: a bright key light
        // from one upper-front side, a dimmer fill from the other side to
        // soften the key's shadows without erasing them, and a subtle rim
        // light from behind to separate the geometry from the background.
        lighting.lights = {
            Light{glm::vec3(0.5f, -0.6f, 0.8f), glm::vec3(1.0f, 1.0f, 1.0f), true},   // key
            Light{glm::vec3(-0.6f, -0.3f, 0.4f), glm::vec3(0.5f, 0.55f, 0.6f), true}, // fill
            Light{glm::vec3(0.0f, 0.9f, 0.3f), glm::vec3(0.4f, 0.35f, 0.3f), true},   // rim/back
        };
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(key + fill + rim)");

    int lightToRemove = -1;
    for (size_t i = 0; i < lighting.lights.size(); ++i) {
        Light& light = lighting.lights[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::Separator();
        ImGui::Checkbox("Enabled", &light.enabled);
        ImGui::SameLine();
        ImGui::Text("Light %d", static_cast<int>(i + 1));
        ImGui::SameLine(ImGui::GetWindowWidth() - 60.0f);
        bool canRemove = lighting.lights.size() > 1;
        ImGui::BeginDisabled(!canRemove);
        if (ImGui::SmallButton("Remove")) lightToRemove = static_cast<int>(i);
        ImGui::EndDisabled();

        float dir[3] = {light.direction.x, light.direction.y, light.direction.z};
        if (ImGui::SliderFloat3("Direction", dir, -1.0f, 1.0f)) {
            light.direction = glm::vec3(dir[0], dir[1], dir[2]);
        }
        float color[3] = {light.color.r, light.color.g, light.color.b};
        if (ImGui::ColorEdit3("Color", color)) {
            light.color = glm::vec3(color[0], color[1], color[2]);
        }
        ImGui::PopID();
    }
    if (lightToRemove >= 0) {
        lighting.lights.erase(lighting.lights.begin() + lightToRemove);
    }

    ImGui::Spacing();
    ImGui::BeginDisabled(static_cast<int>(lighting.lights.size()) >= LightingSettings::kMaxLights);
    if (ImGui::Button("Add Light")) {
        lighting.lights.push_back(Light{});
    }
    ImGui::EndDisabled();

    // Bed heightmap: operator-entered elevation measurements across the
    // bed, visualized as a colored heatmap surface so warp is visible
    // before it ruins a print. Columns/rows are the source of truth here
    // (not a spacing value) -- the operator states the grid directly
    // ("10 columns, 5 rows"), and X/Y spacing is just bed size divided by
    // (cols-1)/(rows-1). bedDirty is reused for every edit here (columns,
    // rows, per-point value, visibility) -- main.cpp's bedDirty handler
    // already rebuilds BedHeightmapRenderer whenever it fires, so this
    // doesn't need its own separate dirty flag threaded through the whole
    // call chain.
    ImGui::Spacing();
    ImGui::Spacing();
    sectionLabel("Bed Heightmap");
    ImGui::TextWrapped("Enter measured bed elevation at each grid point to see where the bed is "
                        "elevated too much, as a heatmap. Point (0,0) is one bed corner, "
                        "(columns-1, rows-1) is the opposite corner.");

    if (ImGui::Checkbox("Show heatmap", &heightmap.visible)) bedDirty = true;

    int cols = heightmap.cols;
    int rows = heightmap.rows;
    bool colsChanged = ImGui::InputInt("Columns", &cols);
    bool rowsChanged = ImGui::InputInt("Rows", &rows);
    if (colsChanged || rowsChanged) {
        heightmap.resize(std::max(cols, 2), std::max(rows, 2));
        bedDirty = true;
    }

    if (ImGui::Button("Reset all to 0")) {
        std::fill(heightmap.elevationsMm.begin(), heightmap.elevationsMm.end(), 0.0f);
        bedDirty = true;
    }

    // Save/Load Bed already exist up in the "Bed size" section above, but
    // this panel is tall enough now (lighting + a 100+ field grid) that
    // scrolling back up just to save entered measurements is annoying --
    // duplicate the same two buttons here. saveBedRequested_/
    // loadBedRequested_ are the exact same request flags the top buttons
    // set; main.cpp's handling already saves/loads the heightmap alongside
    // the rest of the bed settings.
    if (ImGui::Button("Save Bed...##heightmap")) saveBedRequested_ = true;
    ImGui::SameLine();
    if (ImGui::Button("Load Bed...##heightmap")) loadBedRequested_ = true;

    if (heightmap.cols >= 2 && heightmap.rows >= 2) {
        ImGui::Text("%d x %d grid (%d points)", heightmap.cols, heightmap.rows, heightmap.cols * heightmap.rows);
        ImGui::TextDisabled("Drag a value to scrub it, or double-click/Ctrl+click to type an exact number.");

        // Scrollable child so a large grid doesn't blow out the rest of
        // the Bed panel.
        ImGui::BeginChild("heightmapGrid", ImVec2(0, 220), true, ImGuiWindowFlags_HorizontalScrollbar);
        if (ImGui::BeginTable("heightmapTable", heightmap.cols, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit)) {
            // Row 0 (the -Y edge) drawn FIRST. Counterintuitive but
            // correct: Camera's Top preset looks straight down -Z with its
            // "up" vector derived from the SAME orbit quaternion as every
            // other view (see Camera::viewMatrix/orientation) -- at that
            // preset's yaw/pitch, screen-up works out to world -Y, not
            // +Y. So the row that reads at the TOP of this table (and
            // should look like the top of the bed in Top view) is the one
            // at the smallest Y, i.e. row 0. Getting this backwards was a
            // real reported bug: an operator's entered values rendered
            // mirrored top-to-bottom versus where they expected them.
            for (int row = 0; row < heightmap.rows; ++row) {
                ImGui::TableNextRow();
                for (int col = 0; col < heightmap.cols; ++col) {
                    ImGui::TableNextColumn();
                    ImGui::PushID(row * heightmap.cols + col);
                    ImGui::SetNextItemWidth(56.0f);
                    if (ImGui::DragFloat("##v", &heightmap.at(col, row), 0.02f, -50.0f, 50.0f, "%.2f")) {
                        bedDirty = true;
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndTable();
        }
        ImGui::EndChild();
    } else {
        ImGui::TextDisabled("No grid yet -- click \"Resize grid to bed\".");
    }
}

void EditorUI::drawObjectListPanel(Scene& scene, UndoStack& undoStack, bool& dirty) {
    sectionLabel("Objects");
    if (ImGui::BeginTable("objects", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
        ImGui::TableSetupColumn("Visible", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Color", ImGuiTableColumnFlags_WidthFixed, 40.0f);
        ImGui::TableSetupColumn("Reorder", ImGuiTableColumnFlags_WidthFixed, 55.0f);
        ImGui::TableSetupColumn("Link->next", ImGuiTableColumnFlags_WidthFixed, 75.0f);
        ImGui::TableSetupColumn("Delete", ImGuiTableColumnFlags_WidthFixed, 55.0f);
        ImGui::TableHeadersRow();

        int objectIdPendingDelete = -1;

        for (size_t i = 0; i < scene.objects.size(); ++i) {
            SceneObject& object = scene.objects[i];
            ImGui::TableNextRow();
            ImGui::PushID(object.id);

            ImGui::TableNextColumn();
            bool visible = object.visible;
            if (ImGui::Checkbox("##visible", &visible)) {
                undoStack.snapshotBeforeChange(scene);
                object.visible = visible;
                dirty = true;
            }

            ImGui::TableNextColumn();
            bool isActive = (scene.activeObjectId == object.id);
            // NOT ImGuiSelectableFlags_SpanAllColumns: it extends this
            // Selectable's hit-test across every column in the row,
            // INCLUDING the Delete button and checkboxes drawn after it --
            // which was silently swallowing clicks meant for those
            // widgets (a known Dear ImGui table gotcha). A plain
            // Selectable confined to its own column trades "click
            // anywhere in the row to select" for "the other buttons in
            // this row actually work."
            if (ImGui::Selectable(object.name.c_str(), isActive)) {
                scene.activeObjectId = object.id; // not undoable -- active object is a UI cursor, not scene data
            }

            ImGui::TableNextColumn();
            float color[3] = {object.color.r, object.color.g, object.color.b};
            ImGuiColorEditFlags colorFlags = ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel;
            bool colorChanged = ImGui::ColorEdit3("##color", color, colorFlags);
            if (ImGui::IsItemActivated()) undoStack.beginContinuousEdit(scene);
            if (colorChanged) {
                object.color = glm::vec3(color[0], color[1], color[2]);
                dirty = true;
            }
            if (ImGui::IsItemDeactivatedAfterEdit()) undoStack.commitContinuousEdit();

            ImGui::TableNextColumn();
            ImGui::BeginDisabled(i == 0);
            if (ImGui::ArrowButton("##up", ImGuiDir_Up)) {
                undoStack.snapshotBeforeChange(scene);
                std::swap(scene.objects[i], scene.objects[i - 1]);
                dirty = true;
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(i + 1 >= scene.objects.size());
            if (ImGui::ArrowButton("##down", ImGuiDir_Down)) {
                undoStack.snapshotBeforeChange(scene);
                std::swap(scene.objects[i], scene.objects[i + 1]);
                dirty = true;
            }
            ImGui::EndDisabled();

            ImGui::TableNextColumn();
            if (i + 1 < scene.objects.size()) {
                int nextId = scene.objects[i + 1].id;
                bool linked = scene.objectLinks.count({object.id, nextId}) > 0;
                if (ImGui::Checkbox("##link", &linked)) {
                    undoStack.snapshotBeforeChange(scene);
                    scene.toggleLink(object.id, nextId);
                    dirty = true;
                }
            } else {
                ImGui::TextDisabled("--");
            }

            ImGui::TableNextColumn();
            if (ImGui::SmallButton("Delete")) {
                objectIdPendingDelete = object.id;
            }

            ImGui::PopID();
        }
        ImGui::EndTable();

        // Deleting inside the loop above would invalidate scene.objects'
        // iteration (indices shift), so the actual erase happens once,
        // after the table is fully drawn for this frame.
        if (objectIdPendingDelete != -1) {
            undoStack.snapshotBeforeChange(scene);
            auto it = std::find_if(scene.objects.begin(), scene.objects.end(),
                                    [objectIdPendingDelete](const SceneObject& o) { return o.id == objectIdPendingDelete; });
            if (it != scene.objects.end()) {
                scene.objects.erase(it);
                if (scene.activeObjectId == objectIdPendingDelete) {
                    scene.activeObjectId = scene.objects.empty() ? 0 : scene.objects.front().id;
                }
                dirty = true;
            }
        }
    }

    // Toggling "Link->next" above only records the pair in
    // scene.objectLinks -- a PROCEDURAL preview (drawn as a bright
    // magenta line in the viewport, see LinkPreviewRenderer), not real
    // path data. This button converts every currently-pending link into
    // a permanent Travel path on its from-object (editor/ObjectLinking.h),
    // after which it behaves exactly like any other path -- editable,
    // exportable, deletable.
    if (!scene.objectLinks.empty()) {
        ImGui::TextDisabled("%zu pending link(s) (magenta preview in viewport).", scene.objectLinks.size());
        if (ImGui::SmallButton("Bake links to travels")) {
            undoStack.snapshotBeforeChange(scene);
            // Snapshot the pairs first -- bakeLinkToTravel() erases from
            // scene.objectLinks as it goes, which would invalidate an
            // in-progress iteration over the live set.
            std::vector<std::pair<int, int>> pending(scene.objectLinks.begin(), scene.objectLinks.end());
            for (const auto& [fromId, toId] : pending) {
                bakeLinkToTravel(scene, fromId, toId);
            }
            dirty = true;
        }
    }
}

void EditorUI::drawMultiPartPanel(Scene& scene, UndoStack& undoStack, bool& dirty) {
    ImGui::Separator();
    if (boldFont_) ImGui::PushFont(boldFont_);
    bool open = ImGui::CollapsingHeader("Mirror the object", ImGuiTreeNodeFlags_DefaultOpen);
    if (boldFont_) ImGui::PopFont();
    if (!open) return;

    ImGui::TextWrapped("Mirrors the active part, spreads the copies out, and links them layer by layer "
                        "into one program -- part A layer 1, part B layer 1, back to A layer 2, and so "
                        "on -- so each part cools while the others print. The travels between parts are "
                        "tagged for cutting apart afterward, leaving separate finished parts.");

    SceneObject* active = scene.activeObject();
    ImGui::InputInt("Total copies (incl. original)", &multiPartCopies_);
    multiPartCopies_ = std::clamp(multiPartCopies_, 2, 8);
    ImGui::DragFloat("Space between copies (mm)", &multiPartSafeDistanceMm_, 5.0f, 0.0f, 5000.0f, "%.0f");
    ImGui::DragFloat("Detour margin (mm)", &multiPartTravelClearanceMm_, 1.0f, 0.0f, 1000.0f, "%.0f");
    ImGui::TextDisabled("Cross-part travels are FLAT -- they never change Z. With 3+ parts, a move");
    ImGui::TextDisabled("that would cross a middle part detours around it in Y by this much.");
    ImGui::DragFloat("Travel speed", &multiPartTravelSpeed_, 0.01f, 0.01f, 5.0f, "%.2f");

    // ONE button for one intent. This used to be two ("Mirror the
    // object", then "Build interleaved print"), which made the second
    // step look optional and the first look broken when used alone --
    // reported as "mirror doesn't work". Mirroring without the
    // layer-by-layer linking isn't a thing anyone wanted here.
    ImGui::Spacing();
    ImGui::BeginDisabled(active == nullptr);
    if (ImGui::Button("Mirror and link layer by layer")) {
        undoStack.snapshotBeforeChange(scene);
        MirrorInterleaveOptions options;
        options.copies = multiPartCopies_;
        options.gapMm = multiPartSafeDistanceMm_;
        options.detourMarginMm = multiPartTravelClearanceMm_;
        options.travelSpeed = multiPartTravelSpeed_;

        if (auto merged = mirrorAndInterleave(scene, scene.activeObjectId, options)) {
            // Hide the sources rather than deleting them -- the merged
            // object is the thing to export, but destroying the
            // originals would make this hard to recover from.
            for (auto& object : scene.objects) object.visible = false;
            scene.addObject(std::move(*merged));
            scene.activeObjectId = scene.objects.back().id;
            lastMirrorResult_ = "Built: " + scene.objects.back().name;
        } else {
            lastMirrorResult_ = "Failed -- the active object has no detected print layers.";
        }
        dirty = true;
    }
    ImGui::EndDisabled();
    if (active == nullptr) ImGui::TextDisabled("Load a file and select an object first.");
    if (!lastMirrorResult_.empty()) ImGui::TextDisabled("%s", lastMirrorResult_.c_str());
}

void EditorUI::drawTransformPanel(Scene& scene, SceneObject& object, UndoStack& undoStack, bool& dirty) {
    if (boldFont_) ImGui::PushFont(boldFont_);
    bool transformOpen = ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen);
    if (boldFont_) ImGui::PopFont();
    if (!transformOpen) return;

    Transform& t = object.transform;
    ImGui::PushID("transform");

    auto dragField = [&](const char* label, double* value) {
        bool changed = ImGui::InputDouble(label, value, 1.0, 10.0, "%.2f");
        if (ImGui::IsItemActivated()) undoStack.beginContinuousEdit(scene);
        if (changed) dirty = true;
        if (ImGui::IsItemDeactivatedAfterEdit()) undoStack.commitContinuousEdit();
    };
    dragField("X (mm)", &t.x);
    dragField("Y (mm)", &t.y);
    dragField("Z (mm)", &t.z);
    dragField("Rotate Z (deg)", &t.rotZDegrees);

    bool flipX = t.flipX;
    if (ImGui::Checkbox("Flip X", &flipX)) {
        undoStack.snapshotBeforeChange(scene);
        t.flipX = flipX;
        dirty = true;
    }
    ImGui::SameLine();
    bool flipY = t.flipY;
    if (ImGui::Checkbox("Flip Y", &flipY)) {
        undoStack.snapshotBeforeChange(scene);
        t.flipY = flipY;
        dirty = true;
    }

    auto nudge = [&](const char* label, double* value, double delta) {
        if (ImGui::Button(label)) {
            undoStack.snapshotBeforeChange(scene);
            *value += delta;
            dirty = true;
        }
        ImGui::SameLine();
    };
    ImGui::Text("Nudge:");
    ImGui::SameLine();
    nudge("X-50", &t.x, -50.0);
    nudge("X+50", &t.x, 50.0);
    nudge("Y-50", &t.y, -50.0);
    nudge("Y+50", &t.y, 50.0);
    nudge("Z-50", &t.z, -50.0);
    if (ImGui::Button("Z+50")) {
        undoStack.snapshotBeforeChange(scene);
        t.z += 50.0;
        dirty = true;
    }

    if (ImGui::Button("Reset transform")) {
        undoStack.snapshotBeforeChange(scene);
        t = Transform{};
        dirty = true;
    }

    // Start point (the joint-space "first safe position") -- editable
    // here because transforming an object can carry it off the bed, and
    // the operator needs to be able to put it back. Coordinates are shown
    // in WORLD space (what "is it on the bed?" is actually asking) but
    // stored local, so the round-trip through the object's transform is
    // done on the way in and out.
    if (object.startPoint.present) {
        ImGui::Spacing();
        sectionLabel("Start point (first safe position)");
        const JointPose& j = object.startPoint.joints;
        ImGui::TextDisabled("Joint pose: A1 %.2f  A2 %.2f  A3 %.2f", j.a1, j.a2, j.a3);
        ImGui::TextDisabled("            A4 %.2f  A5 %.2f  A6 %.2f", j.a4, j.a5, j.a6);

        if (object.startPoint.position.has_value()) {
            glm::dvec3 world = applyTransform(t, *object.startPoint.position);
            float xyz[3] = {static_cast<float>(world.x), static_cast<float>(world.y), static_cast<float>(world.z)};
            bool changed = ImGui::DragFloat3("World X/Y/Z", xyz, 1.0f, 0.0f, 0.0f, "%.2f");
            if (ImGui::IsItemActivated()) undoStack.beginContinuousEdit(scene);
            if (changed) {
                object.startPoint.position = inverseApplyTransform(t, glm::dvec3(xyz[0], xyz[1], xyz[2]));
                object.startPoint.movedByOperator = true;
                dirty = true;
            }
            if (ImGui::IsItemDeactivatedAfterEdit()) undoStack.commitContinuousEdit();

            if (object.startPoint.movedByOperator) {
                ImGui::TextDisabled("Moved by hand (no longer the auto-derived anchor).");
                ImGui::SameLine();
                if (ImGui::SmallButton("Reset##startpoint")) {
                    undoStack.snapshotBeforeChange(scene);
                    // Back to the anchor the parser chose: the program's
                    // first Cartesian point.
                    if (!object.paths.empty()) {
                        object.startPoint.position = object.paths.front().to;
                        object.startPoint.movedByOperator = false;
                        dirty = true;
                    }
                }
            }
        } else {
            ImGui::TextDisabled("No Cartesian anchor -- this program has no Cartesian move to place it near.");
        }
        ImGui::TextDisabled("Display/planning only: export still writes the original joint move.");
    }

    ImGui::PopID();
}

void EditorUI::drawLayerTablePanel(Scene& scene, SceneObject& object, UndoStack& undoStack, bool& dirty, bool& selectionDirty) {
    if (boldFont_) ImGui::PushFont(boldFont_);
    bool layersOpen = ImGui::CollapsingHeader("Layers", ImGuiTreeNodeFlags_DefaultOpen);
    if (boldFont_) ImGui::PopFont();
    if (!layersOpen) return;

    if (object.layers.empty()) {
        ImGui::TextDisabled("No print layers detected.");
        return;
    }

    ImGui::TextWrapped("Click a row to select that layer's print paths "
                        "(Shift = range-select from the last clicked layer, Ctrl = subtract).");

    if (ImGui::BeginTable("layers", 7, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY,
                           ImVec2(0, 200))) {
        ImGui::TableSetupColumn("Layer");
        ImGui::TableSetupColumn("Z");
        ImGui::TableSetupColumn("Start");
        ImGui::TableSetupColumn("End");
        ImGui::TableSetupColumn("Paths");
        ImGui::TableSetupColumn("Speeds", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("Speed range", ImGuiTableColumnFlags_WidthFixed, 130.0f);
        ImGui::TableHeadersRow();

        for (const auto& layer : object.layers) {
            ImGui::TableNextRow();
            ImGui::PushID(layer.layer);

            ImGui::TableNextColumn();
            char label[32];
            std::snprintf(label, sizeof(label), "%d", layer.layer);
            bool anySelected = false;
            for (int p = layer.startPath; p <= layer.endPath; ++p) {
                if (object.selectedPaths.count(p)) { anySelected = true; break; }
            }
            if (ImGui::Selectable(label, anySelected, ImGuiSelectableFlags_SpanAllColumns)) {
                undoStack.snapshotBeforeChange(scene);
                SelectionCompose compose = currentSelectionCompose();
                if (compose == SelectionCompose::Add && layerSelectionAnchor_ >= 0) {
                    // Shift-click: select every layer between the last
                    // clicked layer (the anchor) and this one, inclusive --
                    // standard range-select (click 3, shift-click 37, get
                    // 3 through 37), not just Add-this-one-layer.
                    int lo = std::min(layerSelectionAnchor_, layer.layer);
                    int hi = std::max(layerSelectionAnchor_, layer.layer);
                    std::vector<int> targets;
                    for (const auto& rangeLayer : object.layers) {
                        if (rangeLayer.layer < lo || rangeLayer.layer > hi) continue;
                        std::vector<int> layerPaths = pathNumbersForLayer(object, rangeLayer.layer);
                        targets.insert(targets.end(), layerPaths.begin(), layerPaths.end());
                    }
                    applySelectionCompose(object.selectedPaths, targets, SelectionCompose::Add);
                } else {
                    std::vector<int> targets = pathNumbersForLayer(object, layer.layer);
                    applySelectionCompose(object.selectedPaths, targets, compose);
                    layerSelectionAnchor_ = layer.layer; // plain/ctrl clicks move the anchor; shift-click ranges from the last one
                }
                selectionDirty = true;
            }

            ImGui::TableNextColumn();
            ImGui::Text("%.3f", layer.z);
            ImGui::TableNextColumn();
            ImGui::Text("%d", layer.startPath);
            ImGui::TableNextColumn();
            ImGui::Text("%d", layer.endPath);
            ImGui::TableNextColumn();
            ImGui::Text("%d", layer.endPath - layer.startPath + 1);

            // A layer is rarely one single speed -- a real sliced layer
            // mixes perimeter/infill speeds, and an operator edit can
            // split it further. Showing the DISTINCT COUNT plus the
            // min-max range answers both "is this layer uniform?" and
            // "what is it actually running at?" without needing a row
            // per path.
            std::vector<double> layerSpeeds;
            for (const auto& p : object.paths) {
                if (p.type != PathType::Print || p.layer != layer.layer) continue;
                double s = p.effectiveSpeed();
                bool seen = false;
                for (double existing : layerSpeeds) {
                    if (std::abs(existing - s) < 1e-9) { seen = true; break; }
                }
                if (!seen) layerSpeeds.push_back(s);
            }
            std::sort(layerSpeeds.begin(), layerSpeeds.end());

            ImGui::TableNextColumn();
            if (layerSpeeds.empty()) {
                ImGui::TextDisabled("--");
            } else if (layerSpeeds.size() == 1) {
                ImGui::Text("1");
            } else {
                // Mixed speeds are worth spotting at a glance -- it's
                // usually intentional (perimeter vs infill) but it's also
                // how a half-applied speed edit shows up.
                ImGui::TextColored(ImVec4(1.0f, 0.80f, 0.35f, 1.0f), "%zu", layerSpeeds.size());
            }

            ImGui::TableNextColumn();
            if (layerSpeeds.empty()) {
                ImGui::TextDisabled("--");
            } else if (layerSpeeds.size() == 1) {
                ImGui::Text("%.4f", layerSpeeds.front());
            } else {
                ImGui::Text("%.4f - %.4f", layerSpeeds.front(), layerSpeeds.back());
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::TextUnformatted("Distinct speeds in this layer:");
                    for (double s : layerSpeeds) ImGui::Text("  %.4f", s);
                    ImGui::EndTooltip();
                }
            }

            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    sectionLabel("Selection & splitting");
    ImGui::Text("Selected paths: %zu", object.selectedPaths.size());
    ImGui::SameLine();
    if (ImGui::SmallButton("Select all visible")) {
        undoStack.snapshotBeforeChange(scene);
        applySelectionCompose(object.selectedPaths, allPathNumbers(object), currentSelectionCompose());
        selectionDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear")) {
        undoStack.snapshotBeforeChange(scene);
        object.selectedPaths.clear();
        selectionDirty = true;
    }
    // Travels carry no layer, so the layer table can never select them --
    // these are the only way to grab them as a group, which matters
    // because a long travel is a common thing to want to slow down or
    // split. Both respect Shift/Ctrl compose like every other selector.
    if (ImGui::SmallButton("Select travels")) {
        undoStack.snapshotBeforeChange(scene);
        applySelectionCompose(object.selectedPaths, travelPathNumbers(object), currentSelectionCompose());
        selectionDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Select prints")) {
        undoStack.snapshotBeforeChange(scene);
        applySelectionCompose(object.selectedPaths, printPathNumbers(object), currentSelectionCompose());
        selectionDirty = true;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(object.selectedPaths.empty());
    if (ImGui::SmallButton("Split selected")) {
        undoStack.snapshotBeforeChange(scene);
        splitSelectedPaths(object);
        dirty = true; // structural change (paths vector grew) -- needs a full rebuild, not just a selection refresh
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled("Split inserts a new vertex at each selected path's midpoint -- handy for");
    ImGui::TextDisabled("giving half of a long travel/print move its own speed. Turn on");
    ImGui::TextDisabled("View > Display > Vertices to see the new point appear.");

    ImGui::Spacing();
    ImGui::SetNextItemWidth(100.0f);
    ImGui::DragFloat("##rotateAngle", &rotateSelectedAngleDeg_, 0.5f, -360.0f, 360.0f, "%.1f deg");
    ImGui::SameLine();
    ImGui::BeginDisabled(object.selectedPaths.empty());
    if (ImGui::SmallButton("Rotate selected")) {
        undoStack.snapshotBeforeChange(scene);
        rotateSelectedPaths(object, rotateSelectedAngleDeg_);
        dirty = true;
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled("Spins the selected paths around THEIR OWN centroid (not the object's pivot).");
    ImGui::TextDisabled("Can leave a gap where a rotated path used to touch an unselected neighbor --");
    ImGui::TextDisabled("same trade-off as Start/End gizmo dragging.");

    ImGui::Spacing();
    sectionLabel("Layer actions");
    ImGui::TextWrapped("Insert a command before a layer's first motion line on export "
                        "(HALT, part cooling, or custom KRL text).");

    // Output indices are EDITABLE, not hardcoded: these default to the
    // mapping an Eidos-generated program uses for this cell (its
    // end-of-program block labels $OUT[5] "AIR COMMAND", $OUT[6] bed
    // heat, $OUT[7] extruder motor), but I/O assignment is per-cell and
    // firing the wrong output on a different machine could do something
    // genuinely unwanted. Surfaced up front so the operator confirms it
    // rather than discovers it.
    ImGui::TextDisabled("Cell I/O mapping -- confirm this matches YOUR cell:");
    ImGui::SetNextItemWidth(140.0f);
    ImGui::InputInt("Cooling/air $OUT", &coolingOutputIndex_);
    coolingOutputIndex_ = std::clamp(coolingOutputIndex_, 1, 4096);

    char coolingOn[128], coolingOff[128];
    std::snprintf(coolingOn, sizeof(coolingOn), "$OUT[%d]=TRUE", coolingOutputIndex_);
    std::snprintf(coolingOff, sizeof(coolingOff), "$OUT[%d]=FALSE", coolingOutputIndex_);

    const char* kPresetLabels[] = {"Halt", "Part cooling ON", "Part cooling OFF", "Custom"};
    const char* kPresetText[] = {"HALT", coolingOn, coolingOff, ""};
    if (ImGui::Combo("Preset", &layerActionPresetIndex_, kPresetLabels, 4)) {
        std::snprintf(layerActionTextBuffer_, sizeof(layerActionTextBuffer_), "%s", kPresetText[layerActionPresetIndex_]);
    }
    ImGui::InputInt("Target layer", &layerActionTargetLayer_);
    layerActionTargetLayer_ = std::clamp(layerActionTargetLayer_, 1, object.layers.empty() ? 1 : object.layers.back().layer);
    ImGui::InputText("KRL text", layerActionTextBuffer_, sizeof(layerActionTextBuffer_));

    // The bug this guards against, found the hard way on a real print:
    // the old presets inserted "; TODO: set the correct output..." --
    // which is a KRL COMMENT. It exported fine, the robot ran fine, and
    // part cooling simply never switched on, with nothing anywhere
    // saying why. Text that is empty or entirely a comment cannot DO
    // anything on the robot, so refuse to add it rather than let it look
    // like it worked.
    std::string trimmedAction = layerActionTextBuffer_;
    trimmedAction.erase(0, trimmedAction.find_first_not_of(" \t"));
    bool actionIsNoOp = trimmedAction.empty() || trimmedAction[0] == ';';

    if (actionIsNoOp) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.30f, 1.0f),
                            "Empty or comment-only -- this would do NOTHING on the robot.");
    }
    ImGui::BeginDisabled(actionIsNoOp);
    if (ImGui::Button("Add layer action")) {
        undoStack.snapshotBeforeChange(scene);
        LayerAction action;
        action.layer = layerActionTargetLayer_;
        action.label = kPresetLabels[layerActionPresetIndex_];
        action.krlText = layerActionTextBuffer_;
        object.layerActions.push_back(action);
        dirty = true;
    }
    ImGui::EndDisabled();

    for (size_t i = 0; i < object.layerActions.size(); ++i) {
        const LayerAction& action = object.layerActions[i];
        ImGui::PushID(static_cast<int>(i) + 10000);
        ImGui::Text("Layer %d: %s (%s)", action.layer, action.label.c_str(), action.krlText.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove")) {
            undoStack.snapshotBeforeChange(scene);
            object.layerActions.erase(object.layerActions.begin() + static_cast<long>(i));
            dirty = true;
            ImGui::PopID();
            break; // vector shrank -- stop iterating this frame
        }
        ImGui::PopID();
    }
}

void EditorUI::drawSelectionGroupPanel(Scene& scene, SceneObject& object, UndoStack& undoStack, bool& dirty, bool& selectionDirty) {
    if (boldFont_) ImGui::PushFont(boldFont_);
    bool groupsOpen = ImGui::CollapsingHeader("Selection groups");
    if (boldFont_) ImGui::PopFont();
    if (!groupsOpen) return;

    ImGui::InputText("Name", groupNameBuffer_, sizeof(groupNameBuffer_));
    ImGui::ColorEdit3("Color", groupColor_);
    ImGui::BeginDisabled(object.selectedPaths.empty());
    if (ImGui::Button("Create group from current selection")) {
        undoStack.snapshotBeforeChange(scene);
        createSelectionGroupFromSelection(object, groupNameBuffer_,
                                           glm::vec3(groupColor_[0], groupColor_[1], groupColor_[2]));
        dirty = true;
    }
    ImGui::EndDisabled();

    for (size_t i = 0; i < object.selectionGroups.size(); ++i) {
        SelectionGroup& group = object.selectionGroups[i];
        ImGui::PushID(group.id.c_str());
        ImGui::Text("%s (%zu paths)", group.name.c_str(), group.pathNumbers.size());
        ImGui::SameLine();
        if (ImGui::SmallButton("Select")) {
            undoStack.snapshotBeforeChange(scene);
            applySelectionCompose(object.selectedPaths, group.pathNumbers, currentSelectionCompose());
            selectionDirty = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Delete")) {
            undoStack.snapshotBeforeChange(scene);
            object.selectionGroups.erase(object.selectionGroups.begin() + static_cast<long>(i));
            ImGui::PopID();
            dirty = true;
            break; // vector shrank, indices are stale -- stop iterating this frame
        }
        ImGui::PopID();
    }
}

void EditorUI::drawSpeedPanel(Scene& scene, SceneObject& object, UndoStack& undoStack, bool& dirty) {
    if (boldFont_) ImGui::PushFont(boldFont_);
    bool speedOpen = ImGui::CollapsingHeader("Speed editing", ImGuiTreeNodeFlags_DefaultOpen);
    if (boldFont_) ImGui::PopFont();
    if (!speedOpen) return;

    if (object.selectedPaths.empty()) {
        ImGui::TextDisabled("Select paths (viewport click/drag, layer table, or group) to edit speed.");
        return;
    }

    std::vector<int> targets(object.selectedPaths.begin(), object.selectedPaths.end());

    // Spell out WHAT is about to be edited. Speed edits apply to travels
    // and print paths alike (only PTP is skipped -- $VEL.CP doesn't
    // control point-to-point motion), so "12 paths selected" alone
    // doesn't tell you whether you're about to change a print speed, a
    // travel speed, or both.
    int printCount = 0, travelCount = 0, ptpCount = 0;
    for (int number : targets) {
        const Path* p = static_cast<const SceneObject&>(object).findPath(number);
        if (!p) continue;
        if (p->motion == "PTP") ++ptpCount;
        else if (p->type == PathType::Travel) ++travelCount;
        else ++printCount;
    }
    ImGui::Text("Selected: %d print, %d travel", printCount, travelCount);
    if (ptpCount > 0) {
        ImGui::TextDisabled("(%d PTP path(s) will be skipped -- $VEL.CP doesn't control PTP motion)", ptpCount);
    }

    ImGui::InputDouble("Exact speed (m/s)", &speedExact_, 0.001, 0.01, "%.4f");
    if (ImGui::Button("Apply exact")) {
        undoStack.snapshotBeforeChange(scene);
        SpeedApplyResult result = applySpeedToPaths(object, targets, SpeedApplyMode::Exact, speedExact_);
        std::printf("Applied exact speed to %d path(s), skipped %d PTP path(s)\n",
                    result.appliedCount, result.skippedPtpCount);
        dirty = true;
    }

    ImGui::InputDouble("Percent", &speedPercent_, 1.0, 5.0, "%.1f");
    ImGui::SameLine();
    if (ImGui::Button("Reduce")) {
        undoStack.snapshotBeforeChange(scene);
        applySpeedToPaths(object, targets, SpeedApplyMode::Reduce, speedPercent_);
        dirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Increase")) {
        undoStack.snapshotBeforeChange(scene);
        applySpeedToPaths(object, targets, SpeedApplyMode::Increase, speedPercent_);
        dirty = true;
    }
}

void EditorUI::drawBedConformPanel(Scene& scene, SceneObject& object, const BedHeightmap& heightmap,
                                    const BedSettings& bed, UndoStack& undoStack, bool& dirty) {
    if (boldFont_) ImGui::PushFont(boldFont_);
    bool open = ImGui::CollapsingHeader("Bed Conform");
    if (boldFont_) ImGui::PopFont();
    if (!open) return;

    if (heightmap.cols < 2 || heightmap.rows < 2) {
        ImGui::TextDisabled("No bed heightmap data yet -- enter measurements in the Bed panel first.");
        return;
    }

    ImGui::TextWrapped("Shifts each print path's Z (and optionally speed) based on the measured bed "
                        "heightmap at its position -- higher bed runs faster, lower bed runs slower.");

    ImGui::InputInt("Affected layers", &bedConformAffectedLayers_);
    bedConformAffectedLayers_ = std::max(bedConformAffectedLayers_, 1);
    ImGui::TextDisabled("Layer 1 gets full effect, tapering to none by layer (affected+1).");

    ImGui::Checkbox("Adjust Z", &bedConformAdjustZ_);
    ImGui::SameLine();
    ImGui::Checkbox("Adjust speed", &bedConformAdjustSpeed_);

    ImGui::BeginDisabled(!bedConformAdjustSpeed_);
    ImGui::DragFloat("Speed gain per mm", &bedConformSpeedGainPerMm_, 0.01f, 0.0f, 1.0f, "%.2f");
    ImGui::EndDisabled();

    ImGui::BeginDisabled(!bedConformAdjustZ_ && !bedConformAdjustSpeed_);
    if (ImGui::Button("Apply bed conform")) {
        undoStack.snapshotBeforeChange(scene);
        BedConformOptions options;
        options.affectedLayers = bedConformAffectedLayers_;
        options.adjustZ = bedConformAdjustZ_;
        options.adjustSpeed = bedConformAdjustSpeed_;
        options.speedGainPerMm = bedConformSpeedGainPerMm_;
        applyBedConform(object, heightmap, bed, options);
        dirty = true;
    }
    ImGui::EndDisabled();
}

void EditorUI::drawColorModePanel(ColorMode& colorMode, bool& dirty) {
    sectionLabel("Color mode");
    const ColorMode modes[] = {ColorMode::Object, ColorMode::Type, ColorMode::Layer,
                                ColorMode::Group, ColorMode::Speed, ColorMode::Sequence};
    for (ColorMode mode : modes) {
        bool selected = (colorMode == mode);
        if (ImGui::RadioButton(colorModeLabel(mode), selected)) {
            if (!selected) {
                colorMode = mode;
                dirty = true;
            }
        }
    }
}

void EditorUI::drawStatsPanel(const Scene& scene, RenderMode mode, size_t renderedPrimitiveCount) {
    size_t totalPaths = 0, totalLayers = 0;
    for (const auto& object : scene.objects) {
        totalPaths += object.paths.size();
        totalLayers += object.layers.size();
    }
    ImGui::Text("%zu object(s), %zu path(s), %zu layer(s)", scene.objects.size(), totalPaths, totalLayers);
    if (mode == RenderMode::Lines) {
        ImGui::Text("%zu line segment(s) on GPU", renderedPrimitiveCount);
    } else {
        ImGui::Text("%zu triangle(s) on GPU (bead geometry)", renderedPrimitiveCount);
    }
}
