#pragma once

#include "model/Layer.h"
#include "model/Path.h"
#include "model/SelectionGroup.h"
#include "model/Transform.h"

#include <glm/glm.hpp>
#include <set>
#include <string>
#include <vector>

// One imported SRC (or eventually G-code) file's worth of motion, treated
// as a single movable, hideable, selectable thing. Mirrors the original's
// per-object shape (objects[i] in index.html) -- see docs/PLAN.md.
struct SceneObject {
    int id = 0;
    std::string name;
    std::vector<std::string> sourceLines; // the original file's lines, kept for future write-back / export
    std::vector<Path> paths;
    std::vector<Layer> layers;
    bool visible = true;
    Transform transform;
    glm::vec3 color{0.35f, 0.80f, 0.95f}; // default per-object color for "color by object" mode

    // The ONE selection mechanism. Every UI action that "selects paths" --
    // manual click, a layer-table row, "select visible", or applying a
    // selection group -- just fills this set. Everything downstream
    // (speed editing, etc.) reads from here and doesn't know or care how it
    // got populated. See docs/PLAN.md's speed-editing note.
    std::set<int> selectedPaths;

    std::vector<SelectionGroup> selectionGroups;

    Path* findPath(int number) {
        for (auto& p : paths) {
            if (p.number == number) return &p;
        }
        return nullptr;
    }
};
