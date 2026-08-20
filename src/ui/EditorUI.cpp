#include "ui/EditorUI.h"
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
                     BedSettings& bedSettings, UndoStack& undoStack, size_t renderedPrimitiveCount,
                     bool& sceneDirty, bool& selectionDirty, bool& bedDirty) {
    drawMenuBar(scene, undoStack, sceneDirty);

    ImGui::SetNextWindowPos(ImVec2(12, 32), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(380, 680), ImGuiCond_FirstUseEver);
    ImGui::Begin("Editor");

    drawViewPanel(camera, renderSettings, sceneDirty);
    ImGui::Separator();
    drawObjectListPanel(scene, undoStack, sceneDirty);

    SceneObject* active = scene.activeObject();
    if (active) {
        ImGui::Separator();
        ImGui::Text("Active: %s", active->name.c_str());
        drawTransformPanel(scene, *active, undoStack, sceneDirty);
        drawLayerTablePanel(scene, *active, undoStack, sceneDirty, selectionDirty);
        drawSelectionGroupPanel(scene, *active, undoStack, sceneDirty, selectionDirty);
        drawSpeedPanel(scene, *active, undoStack, sceneDirty);
    } else {
        ImGui::Separator();
        ImGui::TextDisabled("No object loaded. File > Open to load a .src file.");
    }

    ImGui::Separator();
    drawColorModePanel(colorMode, sceneDirty);

    ImGui::Separator();
    drawStatsPanel(scene, renderSettings.mode, renderedPrimitiveCount);

    ImGui::End();

    float displayWidth = ImGui::GetIO().DisplaySize.x;
    ImGui::SetNextWindowPos(ImVec2(displayWidth - 332, 32), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(320, 320), ImGuiCond_FirstUseEver);
    ImGui::Begin("Bed");
    drawBedPanel(bedSettings, bedDirty);
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
        ImGui::EndMainMenuBar();
    }
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
        if (ImGui::SliderFloat("Bead width (mm)", &renderSettings.beadWidthMm, 0.5f, 50.0f, "%.1f")) dirty = true;
        if (ImGui::SliderFloat("Bead height (mm)", &renderSettings.beadHeightMm, 0.5f, 50.0f, "%.1f")) dirty = true;
        ImGui::TextDisabled("Print paths render as mitered solid bead tubes; travel paths stay as thin lines.");
        // Rendering-only toggle -- no rebuild needed, just checked at draw time.
        ImGui::Checkbox("Backface culling (hide inside of tubes)", &renderSettings.backfaceCulling);
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
}

void EditorUI::drawBedPanel(BedSettings& bed, bool& bedDirty) {
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

    ImGui::Spacing();
    if (ImGui::Button("Save Bed...")) saveBedRequested_ = true;
    ImGui::SameLine();
    if (ImGui::Button("Load Bed...")) loadBedRequested_ = true;
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
                        "(Shift = add, Ctrl = subtract).");

    if (ImGui::BeginTable("layers", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY,
                           ImVec2(0, 180))) {
        ImGui::TableSetupColumn("Layer");
        ImGui::TableSetupColumn("Z");
        ImGui::TableSetupColumn("Start");
        ImGui::TableSetupColumn("End");
        ImGui::TableSetupColumn("Paths");
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
                std::vector<int> targets = pathNumbersForLayer(object, layer.layer);
                applySelectionCompose(object.selectedPaths, targets, currentSelectionCompose());
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

            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::Text("Selected paths: %zu", object.selectedPaths.size());
    ImGui::SameLine();
    if (ImGui::SmallButton("Select all visible")) {
        applySelectionCompose(object.selectedPaths, allPathNumbers(object), currentSelectionCompose());
        selectionDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear")) {
        object.selectedPaths.clear();
        selectionDirty = true;
    }

    ImGui::Spacing();
    sectionLabel("Layer actions");
    ImGui::TextWrapped("Insert a command before a layer's first motion line on export "
                        "(HALT, part cooling, or custom KRL text).");

    static const char* kPresetLabels[] = {"Halt", "Part cooling ON", "Part cooling OFF", "Custom"};
    static const char* kPresetText[] = {"HALT", "; TODO: set the correct output for this cell, e.g. $OUT[12] = TRUE",
                                         "; TODO: set the correct output for this cell, e.g. $OUT[12] = FALSE", ""};
    if (ImGui::Combo("Preset", &layerActionPresetIndex_, kPresetLabels, 4)) {
        std::snprintf(layerActionTextBuffer_, sizeof(layerActionTextBuffer_), "%s", kPresetText[layerActionPresetIndex_]);
    }
    ImGui::InputInt("Target layer", &layerActionTargetLayer_);
    layerActionTargetLayer_ = std::clamp(layerActionTargetLayer_, 1, object.layers.empty() ? 1 : object.layers.back().layer);
    ImGui::InputText("KRL text", layerActionTextBuffer_, sizeof(layerActionTextBuffer_));

    if (ImGui::Button("Add layer action")) {
        undoStack.snapshotBeforeChange(scene);
        LayerAction action;
        action.layer = layerActionTargetLayer_;
        action.label = kPresetLabels[layerActionPresetIndex_];
        action.krlText = layerActionTextBuffer_;
        object.layerActions.push_back(action);
        dirty = true;
    }

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

void EditorUI::drawColorModePanel(ColorMode& colorMode, bool& dirty) {
    sectionLabel("Color mode");
    const ColorMode modes[] = {ColorMode::Object, ColorMode::Type, ColorMode::Layer, ColorMode::Group, ColorMode::Speed};
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
