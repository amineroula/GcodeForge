#include "editor/Picking.h"

#include <algorithm>
#include <limits>

namespace {

// Shortest distance from point `p` to segment [a, b], all in 2D screen space.
float pointToSegmentDistance(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b) {
    glm::vec2 ab = b - a;
    float lenSq = glm::dot(ab, ab);
    float t = (lenSq > 1e-9f) ? glm::dot(p - a, ab) / lenSq : 0.0f;
    t = std::clamp(t, 0.0f, 1.0f);
    glm::vec2 closest = a + ab * t;
    return glm::length(p - closest);
}

} // namespace

std::optional<glm::vec3> ScreenProjector::project(const glm::vec3& worldPoint) const {
    glm::vec4 clip = viewProj * glm::vec4(worldPoint, 1.0f);
    if (clip.w <= 1e-6f) return std::nullopt; // behind the camera
    glm::vec3 ndc = glm::vec3(clip) / clip.w;
    float screenX = (ndc.x * 0.5f + 0.5f) * viewportWidth;
    float screenY = (1.0f - (ndc.y * 0.5f + 0.5f)) * viewportHeight; // NDC is y-up, screen/cursor coords are y-down
    return glm::vec3(screenX, screenY, ndc.z);
}

std::optional<PathRef> pickNearestPath(const Scene& scene, const ScreenProjector& projector,
                                        glm::vec2 screenPoint, float pickRadiusPixels) {
    std::optional<PathRef> best;
    float bestDistance = pickRadiusPixels;
    float bestDepth = std::numeric_limits<float>::max();

    for (const auto& object : scene.objects) {
        if (!object.visible) continue;
        for (const auto& path : object.paths) {
            glm::vec3 fromWorld(applyTransform(object.transform, path.from));
            glm::vec3 toWorld(applyTransform(object.transform, path.to));
            auto fromScreen = projector.project(fromWorld);
            auto toScreen = projector.project(toWorld);
            if (!fromScreen || !toScreen) continue;

            float distance = pointToSegmentDistance(screenPoint, glm::vec2(*fromScreen), glm::vec2(*toScreen));
            float depth = (fromScreen->z + toScreen->z) * 0.5f;

            if (distance <= bestDistance && (distance < bestDistance - 1e-4f || depth < bestDepth)) {
                bestDistance = distance;
                bestDepth = depth;
                best = PathRef{object.id, path.number};
            }
        }
    }
    return best;
}

std::vector<PathRef> pickPathsInRect(const Scene& scene, const ScreenProjector& projector,
                                      glm::vec2 rectMin, glm::vec2 rectMax) {
    std::vector<PathRef> results;

    for (const auto& object : scene.objects) {
        if (!object.visible) continue;
        for (const auto& path : object.paths) {
            glm::vec3 fromWorld(applyTransform(object.transform, path.from));
            glm::vec3 toWorld(applyTransform(object.transform, path.to));
            glm::vec3 midWorld = (fromWorld + toWorld) * 0.5f;
            auto midScreen = projector.project(midWorld);
            if (!midScreen) continue;

            if (midScreen->x >= rectMin.x && midScreen->x <= rectMax.x &&
                midScreen->y >= rectMin.y && midScreen->y <= rectMax.y) {
                results.push_back(PathRef{object.id, path.number});
            }
        }
    }
    return results;
}
