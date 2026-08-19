#pragma once

#include "model/Scene.h"

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
// Ties broken by nearest NDC depth (closer to camera wins).
std::optional<PathRef> pickNearestPath(const Scene& scene, const ScreenProjector& projector,
                                        glm::vec2 screenPoint, float pickRadiusPixels);

// Every path (across all visible objects) whose projected screen-space
// midpoint falls inside the rectangle [rectMin, rectMax] -- used for
// drag/marquee selection. Using the midpoint (not "either endpoint")
// means a segment has to be substantially inside the rectangle, not just
// grazed at one end, which matches how marquee-select conventionally
// feels in CAD/DCC tools.
std::vector<PathRef> pickPathsInRect(const Scene& scene, const ScreenProjector& projector,
                                      glm::vec2 rectMin, glm::vec2 rectMax);
