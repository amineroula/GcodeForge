#include "editor/Visibility.h"
#include "editor/Selection.h"

void hidePaths(SceneObject& object, const std::vector<int>& pathNumbers) {
    for (int number : pathNumbers) object.hiddenPaths.insert(number);
}

void showPaths(SceneObject& object, const std::vector<int>& pathNumbers) {
    for (int number : pathNumbers) object.hiddenPaths.erase(number);
}

void showAllPaths(SceneObject& object) {
    object.hiddenPaths.clear();
}

void hideSelectedPaths(SceneObject& object) {
    hidePaths(object, std::vector<int>(object.selectedPaths.begin(), object.selectedPaths.end()));
}

bool isLayerHidden(const SceneObject& object, int layerNumber) {
    std::vector<int> numbers = pathNumbersForLayer(object, layerNumber);
    if (numbers.empty()) return false;
    for (int number : numbers) {
        if (!object.hiddenPaths.count(number)) return false;
    }
    return true;
}

void setLayerHidden(SceneObject& object, int layerNumber, bool hidden) {
    std::vector<int> numbers = pathNumbersForLayer(object, layerNumber);
    if (hidden) hidePaths(object, numbers);
    else showPaths(object, numbers);
}

bool isGroupHidden(const SceneObject& object, const SelectionGroup& group) {
    if (group.pathNumbers.empty()) return false;
    for (int number : group.pathNumbers) {
        if (!object.hiddenPaths.count(number)) return false;
    }
    return true;
}

void setGroupHidden(SceneObject& object, const SelectionGroup& group, bool hidden) {
    if (hidden) hidePaths(object, group.pathNumbers);
    else showPaths(object, group.pathNumbers);
}
