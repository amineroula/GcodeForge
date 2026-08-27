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

// Liang-Barsky segment-vs-axis-aligned-rectangle clipping: true if any
// part of [a,b] falls inside [rectMin,rectMax] -- the segment just needs
// to TOUCH the box, not have its midpoint fully inside it (that was the
// original, overly strict behavior: a long path only grazing a corner of
// the marquee wouldn't get picked even though visually it's "in the box").
// Handles a zero-length segment (a==b) correctly too: it reduces to a
// plain point-in-rect test.
bool segmentIntersectsRect(const glm::vec2& a, const glm::vec2& b, const glm::vec2& rectMin, const glm::vec2& rectMax) {
    float dx = b.x - a.x;
    float dy = b.y - a.y;
    float tMin = 0.0f, tMax = 1.0f;

    float p[4] = {-dx, dx, -dy, dy};
    float q[4] = {a.x - rectMin.x, rectMax.x - a.x, a.y - rectMin.y, rectMax.y - a.y};

    for (int i = 0; i < 4; ++i) {
        if (std::abs(p[i]) < 1e-9f) {
            if (q[i] < 0.0f) return false; // segment parallel to this edge and entirely on the outside
        } else {
            float t = q[i] / p[i];
            if (p[i] < 0.0f) tMin = std::max(tMin, t);
            else tMax = std::min(tMax, t);
            if (tMin > tMax) return false;
        }
    }
    return true;
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
                                        glm::vec2 screenPoint, float pickRadiusPixels,
                                        bool selectBackfacing) {
    std::optional<PathRef> best;
    float bestDistance = std::numeric_limits<float>::max();
    float bestDepth = std::numeric_limits<float>::max();

    for (const auto& object : scene.objects) {
        if (!object.visible) continue;
        for (const auto& path : object.paths) {
            if (object.hiddenPaths.count(path.number)) continue;
            glm::vec3 fromWorld(applyTransform(object.transform, path.from));
            glm::vec3 toWorld(applyTransform(object.transform, path.to));
            auto fromScreen = projector.project(fromWorld);
            auto toScreen = projector.project(toWorld);
            if (!fromScreen || !toScreen) continue;

            float distance = pointToSegmentDistance(screenPoint, glm::vec2(*fromScreen), glm::vec2(*toScreen));
            if (distance > pickRadiusPixels) continue;
            float depth = (fromScreen->z + toScreen->z) * 0.5f;

            bool better;
            if (selectBackfacing) {
                better = distance < bestDistance;
            } else if (std::abs(depth - bestDepth) < 1e-4f) {
                better = distance < bestDistance; // near-identical depth: fall back to 2D proximity
            } else {
                better = depth < bestDepth; // prefer whichever is closer to the camera
            }

            if (better) {
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
            if (object.hiddenPaths.count(path.number)) continue;
            glm::vec3 fromWorld(applyTransform(object.transform, path.from));
            glm::vec3 toWorld(applyTransform(object.transform, path.to));
            auto fromScreen = projector.project(fromWorld);
            auto toScreen = projector.project(toWorld);
            // If either endpoint is behind the camera, fall back to just
            // the one that's valid (still lets a partially-offscreen path
            // get picked); skip only if BOTH are invalid.
            if (!fromScreen && !toScreen) continue;
            glm::vec2 a = fromScreen ? glm::vec2(*fromScreen) : glm::vec2(*toScreen);
            glm::vec2 b = toScreen ? glm::vec2(*toScreen) : glm::vec2(*fromScreen);

            if (segmentIntersectsRect(a, b, rectMin, rectMax)) {
                results.push_back(PathRef{object.id, path.number});
            }
        }
    }
    return results;
}

std::optional<HeightmapVertexRef> pickNearestHeightmapVertex(const BedHeightmap& heightmap, const BedSettings& bed,
                                                               const ScreenProjector& projector,
                                                               glm::vec2 screenPoint, float pickRadiusPixels) {
    if (heightmap.cols < 2 || heightmap.rows < 2) return std::nullopt;

    float halfWidth = bed.widthMm * 0.5f;
    float halfDepth = bed.depthMm * 0.5f;
    float spacingX = bed.widthMm / static_cast<float>(heightmap.cols - 1);
    float spacingY = bed.depthMm / static_cast<float>(heightmap.rows - 1);

    std::optional<HeightmapVertexRef> best;
    float bestDistance = std::numeric_limits<float>::max();

    for (int row = 0; row < heightmap.rows; ++row) {
        for (int col = 0; col < heightmap.cols; ++col) {
            // Same formula as BedHeightmapRenderer's own vertex build --
            // must match exactly, or a click would land on where the
            // vertex WOULD be at zero elevation, not where it's actually
            // drawn once bumped.
            glm::vec3 world(bed.originXMm - halfWidth + col * spacingX,
                             bed.originYMm - halfDepth + row * spacingY,
                             bed.originZMm + heightmap.at(col, row));
            auto screen = projector.project(world);
            if (!screen) continue;

            float distance = glm::length(screenPoint - glm::vec2(*screen));
            if (distance > pickRadiusPixels) continue;
            if (distance < bestDistance) {
                bestDistance = distance;
                best = HeightmapVertexRef{col, row};
            }
        }
    }
    return best;
}
