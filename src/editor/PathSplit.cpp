#include "editor/PathSplit.h"

#include <algorithm>
#include <vector>

void splitSelectedPaths(SceneObject& object) {
    if (object.selectedPaths.empty()) return;

    int nextNumber = 0;
    for (const auto& p : object.paths) nextNumber = std::max(nextNumber, p.number);
    ++nextNumber;

    // Collect the VECTOR indices of selected paths, highest first, so
    // inserting a new element before an earlier index never invalidates
    // an index this loop hasn't processed yet.
    std::vector<size_t> indices;
    for (size_t i = 0; i < object.paths.size(); ++i) {
        if (object.selectedPaths.count(object.paths[i].number)) indices.push_back(i);
    }
    std::sort(indices.begin(), indices.end(), std::greater<size_t>());

    for (size_t index : indices) {
        Path& original = object.paths[index];
        glm::dvec3 midpoint = (original.from + original.to) * 0.5;

        // Copies type/layer/motion/speed/speedOverride/a/b/c from the
        // original -- both halves are the "same kind" of move, just
        // shorter, until the operator edits one of them separately.
        Path firstHalf = original;
        firstHalf.to = midpoint;
        firstHalf.number = nextNumber++;
        firstHalf.srcLine = -1;
        firstHalf.cloneTemplateSrcLine = original.srcLine;

        original.from = midpoint; // original path keeps its number/srcLine, now covers midpoint->B only

        object.paths.insert(object.paths.begin() + static_cast<long>(index), firstHalf);
    }
}
