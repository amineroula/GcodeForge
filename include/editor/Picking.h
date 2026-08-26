#pragma once

#include "model/BedHeightmap.h"
#include "model/Scene.h"
#include "render/BedSettings.h"

#include <glm/glm.hpp>
#include <optional>
#include <vector>

// Viewport path picking: screen-space projection + nearest-segment /
// rectangle tests. Deliberately screen-space rather than a 3D ray-vs-
// segment test -- projecting each path's endpoints once and measuring 2D
// pixel distance is simpler, avoids depth-ambiguity edge cases, and is how
// picking against thin line art is conventionally done. No spatial
// acceleration structure: a brute-force O(paths) scan runs once per click,
// not per frame, so even 100k+ segments is well under a millisecond --
// this is NOT the milestone 11 LOD system, it doesn't need to be.
struct ScreenProjector {
    glm::mat4 viewProj;
    float viewportWidth = 1.0f;
    float viewportHeight = 1.0f;

    // Returns screen-space pixel coordinates (x right, y DOWN, matching
    // GLFW cursor conventions) plus NDC depth, or nullopt if the point is
    // behind the camera (w <= 0) and therefore not meaningfully "on screen".
    std::optional<glm::vec3> project(const glm::vec3& worldPoint) const;
};

struct PathRef {
    int objectId = 0;
    int pathNumber = 0;
};

// The single nearest path (across all visible objects) whose projected
// screen-space segment passes within pickRadiusPixels of screenPoint.
//
// selectBackfacing controls how ties/near-ties are resolved: false
// (default) prefers whichever candidate is nearest the CAMERA (smallest
// NDC depth) among those within the pick radius -- clicking where a solid
// bead's surface is in front of a hidden path behind it picks the visible
// one. true ignores depth entirely and picks by 2D screen distance alone,
// letting you reach a path that's behind/inside other geometry ("select
// backfacing geometry"). This is a screen-space approximation of
// occlusion, not a true depth-buffer test -- it only compares candidates
// that are already close enough on screen to be plausibly what was
// clicked, not every occluder in the scene.
std::optional<PathRef> pickNearestPath(const Scene& scene, const ScreenProjector& projector,
                                        glm::vec2 screenPoint, float pickRadiusPixels,
                                        bool selectBackfacing = false);

// Every path (across all visible objects) whose projected screen-space
// segment intersects the rectangle [rectMin, rectMax] at all (not
// required to have its midpoint inside -- a path only grazing a corner of
// the marquee still counts, matching how marquee-select conventionally
// feels: if you can see part of it in the box, it gets selected).
// Not affected by selectBackfacing -- marquee-select conventionally grabs
// everything in the box regardless of what's in front of what.
std::vector<PathRef> pickPathsInRect(const Scene& scene, const ScreenProjector& projector,
                                      glm::vec2 rectMin, glm::vec2 rectMax);

struct HeightmapVertexRef {
    int col = 0;
    int row = 0;
};

// The single nearest heightmap grid vertex (by projected screen-space
// distance, same brute-force approach as pickNearestPath) within
// pickRadiusPixels of screenPoint -- used by the "click a vertex to
// nudge its Z" bed-painting tool. World position per vertex mirrors
// BedHeightmapRenderer's own vertex-position formula exactly (bed
// origin/size + the vertex's OWN current elevation), so a click lands on
// the vertex as it's actually rendered, bumps and all -- not a flat
// plane approximation.
std::optional<HeightmapVertexRef> pickNearestHeightmapVertex(const BedHeightmap& heightmap, const BedSettings& bed,
                                                               const ScreenProjector& projector,
                                                               glm::vec2 screenPoint, float pickRadiusPixels);
