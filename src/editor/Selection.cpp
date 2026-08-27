#include "editor/Selection.h"

void applySelectionCompose(std::set<int>& selected, const std::vector<int>& targets, SelectionCompose mode) {
    if (mode == SelectionCompose::Replace) selected.clear();
    for (int number : targets) {
        if (mode == SelectionCompose::Subtract) {
            selected.erase(number);
        } else {
            selected.insert(number);
        }
    }
}

// NOT filtered by hiddenPaths, deliberately -- editor/Visibility.h's
// setLayerHidden()/isLayerHidden() depend on this returning EVERY path
// in the layer regardless of current hidden state (un-hiding needs to
// find the very paths that are already hidden). Selection call sites
// that want "only the currently visible ones" filter separately -- see
// EditorUI.cpp's layer-table click handler.
std::vector<int> pathNumbersForLayer(const SceneObject& object, int layerNumber) {
    std::vector<int> result;
    for (const auto& path : object.paths) {
        if (path.type == PathType::Print && path.layer == layerNumber) {
            result.push_back(path.number);
        }
    }
    return result;
}

std::vector<int> travelPathNumbers(const SceneObject& object) {
    std::vector<int> result;
    if (!object.visible) return result;
    for (const auto& path : object.paths) {
        if (path.type == PathType::Travel && !object.hiddenPaths.count(path.number)) result.push_back(path.number);
    }
    return result;
}

std::vector<int> printPathNumbers(const SceneObject& object) {
    std::vector<int> result;
    if (!object.visible) return result;
    for (const auto& path : object.paths) {
        if (path.type == PathType::Print && !object.hiddenPaths.count(path.number)) result.push_back(path.number);
    }
    return result;
}

std::vector<int> allPathNumbers(const SceneObject& object) {
    std::vector<int> result;
    if (!object.visible) return result;
    result.reserve(object.paths.size());
    for (const auto& path : object.paths) {
        if (!object.hiddenPaths.count(path.number)) result.push_back(path.number);
    }
    return result;
}

SelectionGroup& createSelectionGroupFromSelection(SceneObject& object, const std::string& name, glm::vec3 color) {
    SelectionGroup group;
    group.id = name + "#" + std::to_string(object.selectionGroups.size() + 1);
    group.name = name;
    group.color = color;
    group.pathNumbers.assign(object.selectedPaths.begin(), object.selectedPaths.end());
    object.selectionGroups.push_back(std::move(group));
    return object.selectionGroups.back();
}
