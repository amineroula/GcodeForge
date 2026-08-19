#pragma once

#include <glm/glm.hpp>
#include <string>
#include <vector>

// A named, reusable set of path numbers within one object, with its own
// display color. One of the four ways of populating an object's
// selectedPaths (see docs/PLAN.md's speed-editing note) -- create a group
// from the current selection, then re-select or re-color it later without
// re-picking the same paths by hand.
struct SelectionGroup {
    std::string id;
    std::string name;
    glm::vec3 color{0.212f, 0.663f, 1.0f}; // default matches the original's #36a9ff
    std::vector<int> pathNumbers;
};
