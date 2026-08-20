#include "editor/ConnectedDrag.h"

#include <map>

namespace {
constexpr double kConnectivityEpsilon = 1e-4;
}

std::vector<PathDragSnapshot> buildDragSnapshots(const SceneObject& object, GizmoTargetMode mode) {
    std::map<int, PathDragSnapshot> byNumber;

    auto ensure = [&](const Path& p) -> PathDragSnapshot& {
        auto it = byNumber.find(p.number);
        if (it != byNumber.end()) return it->second;
        PathDragSnapshot snap;
        snap.pathNumber = p.number;
        snap.startFrom = p.from;
        snap.startTo = p.to;
        return byNumber.emplace(p.number, snap).first->second;
    };

    bool moveFrom = (mode == GizmoTargetMode::Start || mode == GizmoTargetMode::Whole);
    bool moveTo = (mode == GizmoTargetMode::End || mode == GizmoTargetMode::Whole);

    for (size_t i = 0; i < object.paths.size(); ++i) {
        const Path& p = object.paths[i];
        if (!object.selectedPaths.count(p.number)) continue;

        PathDragSnapshot& snap = ensure(p);
        snap.moveFrom = snap.moveFrom || moveFrom;
        snap.moveTo = snap.moveTo || moveTo;

        if (moveFrom && i > 0) {
            const Path& prev = object.paths[i - 1];
            bool prevAlreadySelected = object.selectedPaths.count(prev.number) > 0;
            bool touching = glm::length(prev.to - p.from) < kConnectivityEpsilon;
            if (!prevAlreadySelected && touching) {
                ensure(prev).moveTo = true;
            }
        }
        if (moveTo && i + 1 < object.paths.size()) {
            const Path& next = object.paths[i + 1];
            bool nextAlreadySelected = object.selectedPaths.count(next.number) > 0;
            bool touching = glm::length(next.from - p.to) < kConnectivityEpsilon;
            if (!nextAlreadySelected && touching) {
                ensure(next).moveFrom = true;
            }
        }
    }

    std::vector<PathDragSnapshot> result;
    result.reserve(byNumber.size());
    for (auto& [number, snap] : byNumber) {
        (void)number;
        result.push_back(snap);
    }
    return result;
}
