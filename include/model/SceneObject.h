#pragma once

#include "model/Layer.h"
#include "model/LayerAction.h"
#include "model/Path.h"
#include "model/SelectionGroup.h"
#include "model/StartPoint.h"
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
    std::vector<std::string> sourceLines; // the original file's lines -- editor/SrcExporter patches specific lines of a COPY of this, never mutates it directly
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

    // Operator-inserted commands at the start of specific layers (HALT,
    // part cooling on/off, etc.) -- see model/LayerAction.h and
    // editor/SrcExporter.h.
    std::vector<LayerAction> layerActions;

    // The joint-space "first safe position" PTP move, if this program has
    // one -- see model/StartPoint.h. Not a Path: it carries no Cartesian
    // coordinates of its own, so it can't participate in path selection,
    // speed editing, or run/mesh building the way real motion paths do.
    StartPoint startPoint;

    Path* findPath(int number) {
        for (auto& p : paths) {
            if (p.number == number) return &p;
        }
        return nullptr;
    }
};
