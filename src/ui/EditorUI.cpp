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

void EditorUI::draw(Scene& scene, ColorMode& colorMode, Camera& camera, size_t renderedLineCount, bool& sceneDirty) {
    drawMenuBar();

    ImGui::SetNextWindowPos(ImVec2(12, 32), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(360, 640), ImGuiCond_FirstUseEver);
    ImGui::Begin("Editor");

    drawViewPanel(camera);
    ImGui::Separator();
    drawObjectListPanel(scene, sceneDirty);

    SceneObject* active = scene.activeObject();
    if (active) {
        ImGui::Separator();
        ImGui::Text("Active: %s", active->name.c_str());
        drawTransformPanel(*active, sceneDirty);
        drawLayerTablePanel(*active, sceneDirty);
        drawSelectionGroupPanel(*active, sceneDirty);
        drawSpeedPanel(*active, sceneDirty);
    } else {
        ImGui::Separator();
        ImGui::TextDisabled("No object loaded. File > Open to load a .src file.");
    }

    ImGui::Separator();
    drawColorModePanel(colorMode, sceneDirty);

    ImGui::Separator();
    drawStatsPanel(scene, renderedLineCount);

    ImGui::End();
}

void EditorUI::drawMenuBar() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open SRC / G-code...")) {
                openFileRequested_ = true;
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
}

void EditorUI::drawViewPanel(Camera& camera) {
    ImGui::Text("View");

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
}

void EditorUI::drawObjectListPanel(Scene& scene, bool& dirty) {
    ImGui::Text("Objects");
    if (ImGui::BeginTable("objects", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Visible", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("Reorder", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("Link->next", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableHeadersRow();

        for (size_t i = 0; i < scene.objects.size(); ++i) {
            SceneObject& object = scene.objects[i];
            ImGui::TableNextRow();
            ImGui::PushID(object.id);

            ImGui::TableNextColumn();
            bool isActive = (scene.activeObjectId == object.id);
            if (ImGui::Selectable(object.name.c_str(), isActive, ImGuiSelectableFlags_SpanAllColumns)) {
                scene.activeObjectId = object.id;
            }

            ImGui::TableNextColumn();
            if (ImGui::Checkbox("##visible", &object.visible)) dirty = true;

            ImGui::TableNextColumn();
            ImGui::BeginDisabled(i == 0);
            if (ImGui::ArrowButton("##up", ImGuiDir_Up)) std::swap(scene.objects[i], scene.objects[i - 1]);
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(i + 1 >= scene.objects.size());
            if (ImGui::ArrowButton("##down", ImGuiDir_Down)) std::swap(scene.objects[i], scene.objects[i + 1]);
            ImGui::EndDisabled();

            ImGui::TableNextColumn();
            if (i + 1 < scene.objects.size()) {
                int nextId = scene.objects[i + 1].id;
                bool linked = scene.objectLinks.count({object.id, nextId}) > 0;
                if (ImGui::Checkbox("##link", &linked)) {
                    scene.toggleLink(object.id, nextId);
                }
            } else {
                ImGui::TextDisabled("--");
            }

            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

void EditorUI::drawTransformPanel(SceneObject& object, bool& dirty) {
    if (!ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) return;

    Transform& t = object.transform;
    ImGui::PushID("transform");

    if (ImGui::InputDouble("X (mm)", &t.x, 1.0, 10.0, "%.2f")) dirty = true;
    if (ImGui::InputDouble("Y (mm)", &t.y, 1.0, 10.0, "%.2f")) dirty = true;
    if (ImGui::InputDouble("Z (mm)", &t.z, 1.0, 10.0, "%.2f")) dirty = true;
    if (ImGui::InputDouble("Rotate Z (deg)", &t.rotZDegrees, 1.0, 10.0, "%.2f")) dirty = true;
    if (ImGui::Checkbox("Flip X", &t.flipX)) dirty = true;
    ImGui::SameLine();
    if (ImGui::Checkbox("Flip Y", &t.flipY)) dirty = true;

    ImGui::Text("Nudge:");
    ImGui::SameLine();
    if (ImGui::Button("X-50")) { t.x -= 50.0; dirty = true; }
    ImGui::SameLine();
    if (ImGui::Button("X+50")) { t.x += 50.0; dirty = true; }
    ImGui::SameLine();
    if (ImGui::Button("Y-50")) { t.y -= 50.0; dirty = true; }
    ImGui::SameLine();
    if (ImGui::Button("Y+50")) { t.y += 50.0; dirty = true; }
    ImGui::SameLine();
    if (ImGui::Button("Z-50")) { t.z -= 50.0; dirty = true; }
    ImGui::SameLine();
    if (ImGui::Button("Z+50")) { t.z += 50.0; dirty = true; }

    if (ImGui::Button("Reset transform")) {
        t = Transform{};
        dirty = true;
    }

    ImGui::PopID();
}

void EditorUI::drawLayerTablePanel(SceneObject& object, bool& dirty) {
    if (!ImGui::CollapsingHeader("Layers", ImGuiTreeNodeFlags_DefaultOpen)) return;

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
                dirty = true;
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
        dirty = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear")) {
        object.selectedPaths.clear();
        dirty = true;
    }
}

void EditorUI::drawSelectionGroupPanel(SceneObject& object, bool& dirty) {
    if (!ImGui::CollapsingHeader("Selection groups")) return;

    ImGui::InputText("Name", groupNameBuffer_, sizeof(groupNameBuffer_));
    ImGui::ColorEdit3("Color", groupColor_);
    ImGui::BeginDisabled(object.selectedPaths.empty());
    if (ImGui::Button("Create group from current selection")) {
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
            dirty = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Delete")) {
            object.selectionGroups.erase(object.selectionGroups.begin() + static_cast<long>(i));
            ImGui::PopID();
            dirty = true;
            break; // vector shrank, indices are stale -- stop iterating this frame
        }
        ImGui::PopID();
    }
}

void EditorUI::drawSpeedPanel(SceneObject& object, bool& dirty) {
    if (!ImGui::CollapsingHeader("Speed editing", ImGuiTreeNodeFlags_DefaultOpen)) return;

    if (object.selectedPaths.empty()) {
        ImGui::TextDisabled("Select paths (layer table, group, or 'select all visible') to edit speed.");
        return;
    }

    std::vector<int> targets(object.selectedPaths.begin(), object.selectedPaths.end());

    ImGui::InputDouble("Exact speed (m/s)", &speedExact_, 0.001, 0.01, "%.4f");
    if (ImGui::Button("Apply exact")) {
        SpeedApplyResult result = applySpeedToPaths(object, targets, SpeedApplyMode::Exact, speedExact_);
        std::printf("Applied exact speed to %d path(s), skipped %d PTP path(s)\n",
                    result.appliedCount, result.skippedPtpCount);
        dirty = true;
    }

    ImGui::InputDouble("Percent", &speedPercent_, 1.0, 5.0, "%.1f");
    ImGui::SameLine();
    if (ImGui::Button("Reduce")) {
        applySpeedToPaths(object, targets, SpeedApplyMode::Reduce, speedPercent_);
        dirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Increase")) {
        applySpeedToPaths(object, targets, SpeedApplyMode::Increase, speedPercent_);
        dirty = true;
    }
}

void EditorUI::drawColorModePanel(ColorMode& colorMode, bool& dirty) {
    ImGui::Text("Color mode");
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

void EditorUI::drawStatsPanel(const Scene& scene, size_t renderedLineCount) {
    size_t totalPaths = 0, totalLayers = 0;
    for (const auto& object : scene.objects) {
        totalPaths += object.paths.size();
        totalLayers += object.layers.size();
    }
    ImGui::Text("%zu object(s), %zu path(s), %zu layer(s)", scene.objects.size(), totalPaths, totalLayers);
    ImGui::Text("%zu line segment(s) on GPU", renderedLineCount);
}
