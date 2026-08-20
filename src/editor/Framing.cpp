#include "editor/Framing.h"

#include <limits>

namespace {

struct BoundsAccumulator {
    glm::vec3 min{std::numeric_limits<float>::max()};
    glm::vec3 max{std::numeric_limits<float>::lowest()};
    bool any = false;
};

void include(BoundsAccumulator& acc, const glm::vec3& p) {
    acc.min = glm::min(acc.min, p);
    acc.max = glm::max(acc.max, p);
    acc.any = true;
}

std::optional<FrameBounds> finish(const BoundsAccumulator& acc) {
    if (!acc.any) return std::nullopt;
    glm::vec3 center = (acc.min + acc.max) * 0.5f;
    float radius = glm::length(acc.max - center);
    if (radius < 1.0f) radius = 1.0f; // degenerate (single point / zero-length path)
    return FrameBounds{center, radius};
}

} // namespace

std::optional<FrameBounds> computeFrameBounds(const Scene& scene, bool preferSelection) {
    if (preferSelection) {
        BoundsAccumulator selectionAcc;
        for (const auto& object : scene.objects) {
            if (!object.visible || object.selectedPaths.empty()) continue;
            for (const auto& path : object.paths) {
                if (!object.selectedPaths.count(path.number)) continue;
                include(selectionAcc, glm::vec3(applyTransform(object.transform, path.from)));
                include(selectionAcc, glm::vec3(applyTransform(object.transform, path.to)));
            }
        }
        if (auto result = finish(selectionAcc)) return result;
        // No selection anywhere -- fall through to "frame all".
    }

    BoundsAccumulator allAcc;
    for (const auto& object : scene.objects) {
        if (!object.visible) continue;
        for (const auto& path : object.paths) {
            include(allAcc, glm::vec3(applyTransform(object.transform, path.from)));
            include(allAcc, glm::vec3(applyTransform(object.transform, path.to)));
        }
    }
    return finish(allAcc);
}
