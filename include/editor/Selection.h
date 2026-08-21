#pragma once

#include "model/SceneObject.h"

#include <set>
#include <string>
#include <vector>

// How a new set of path numbers combines with an object's existing
// selection. Mirrors the original's replace/add/subtract selection
// behavior (docs/PLAN.md task 7) -- normally click = Replace,
// shift-click = Add, ctrl-click = Subtract.
enum class SelectionCompose {
    Replace,
    Add,
    Subtract
};

void applySelectionCompose(std::set<int>& selected, const std::vector<int>& targets, SelectionCompose mode);

// All print paths belonging to one layer -- what clicking a layer-table row
// selects (see docs/PLAN.md's speed-editing note / EVOLUTION.md's layer
// table description).
std::vector<int> pathNumbersForLayer(const SceneObject& object, int layerNumber);

// All paths belonging to a visible object (object-level visibility only --
// there's no per-path hide yet, unlike the original's finer-grained
// visibility filters).
std::vector<int> allPathNumbers(const SceneObject& object);

// All TRAVEL paths, and all PRINT paths, respectively. Travels can be
// selected, split, and speed-edited exactly like print paths -- the only
// thing that ever excluded them was that nothing offered a way to grab
// them as a group (the layer table is print-only by definition, since
// travels carry no layer). A long travel is a common thing to want to
// slow down or split, which is exactly why these exist.
std::vector<int> travelPathNumbers(const SceneObject& object);
std::vector<int> printPathNumbers(const SceneObject& object);

SelectionGroup& createSelectionGroupFromSelection(SceneObject& object, const std::string& name, glm::vec3 color);
