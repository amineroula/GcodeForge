#pragma once

#include "model/SceneObject.h"

#include <vector>

// Per-path viewport visibility, layered on top of the existing
// object-level `visible` flag. Hiding is a VIEWING aid only -- exactly
// like object.visible, hidden paths still export and still print; this
// only affects what GeometryRenderer draws. Layers and selection groups
// aren't separately-tracked visibility state -- "hide a layer" and "hide
// a group" both just resolve to a set of path numbers and hide those,
// via the SAME underlying `SceneObject::hiddenPaths` set the object list
// and layer table both read.
void hidePaths(SceneObject& object, const std::vector<int>& pathNumbers);
void showPaths(SceneObject& object, const std::vector<int>& pathNumbers);
void showAllPaths(SceneObject& object);

// Hides/shows whatever is currently in object.selectedPaths.
void hideSelectedPaths(SceneObject& object);

// Hides every path NOT currently in object.selectedPaths -- the
// isolate-by-selection equivalent of hideSelectedPaths(), replacing the
// earlier per-layer "Iso" button with one general action that works off
// whatever's selected (a layer-table click, a marquee, a selection
// group, travels/prints, anything applySelectionCompose() can produce),
// not just one layer at a time.
void hideUnselectedPaths(SceneObject& object);

// A layer's visibility is derived, not stored: hidden if every path in
// pathNumbersForLayer() is currently in hiddenPaths (so a layer with a
// mix of hidden/shown paths reads as "shown", matching the ImGui
// tri-state-less checkbox: unchecked means "not fully hidden").
bool isLayerHidden(const SceneObject& object, int layerNumber);
void setLayerHidden(SceneObject& object, int layerNumber, bool hidden);

bool isGroupHidden(const SceneObject& object, const SelectionGroup& group);
void setGroupHidden(SceneObject& object, const SelectionGroup& group, bool hidden);
