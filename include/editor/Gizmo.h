#pragma once

#include <glm/glm.hpp>
#include <optional>
#include <vector>

// The math behind a translate gizmo: turning a screen-space mouse position
// into a 3D world-space ray, and finding where that ray comes closest to
// one of the gizmo's axis lines. This is the standard technique every
// 3D tool's move-gizmo uses -- there's no way to intersect a 2D cursor with
// a 1D line in 3D space exactly (the ray generally misses the line
// entirely), so instead you solve for the point on the axis line that
// minimizes distance to the ray. As the mouse moves, that closest point
// slides along the axis -- and since the axis direction is a unit vector,
// how far it slides IS the translation distance, directly.

struct Ray {
    glm::vec3 origin;
    glm::vec3 direction; // unit length
};

// Converts a screen-space point (pixels, y-down) into a world-space ray,
// via the inverse of the same view-projection matrix used to render.
Ray unprojectRay(const glm::mat4& viewProj, glm::vec2 screenPoint, float viewportWidth, float viewportHeight);

// Closest-point-between-two-lines: returns the parametric distance along
// `axisDir` (must be unit length) from `axisOrigin` to the point on that
// infinite line closest to `ray`. std::nullopt only in the degenerate case
// where the ray is parallel to the axis (no unique closest point).
//
// IMPORTANT for callers doing a drag: `axisOrigin` must be a FIXED
// reference point captured once at drag start, not recomputed each frame
// from a value the drag itself is changing -- the returned distance is
// relative to axisOrigin, so a moving reference point makes frame-to-frame
// deltas meaningless (they'd be relative to a different basis each time).
std::optional<float> closestPointOnAxisToRay(const glm::vec3& axisOrigin, const glm::vec3& axisDir, const Ray& ray);

enum class GizmoAxis { X, Y, Z };

glm::vec3 gizmoAxisDirection(GizmoAxis axis);

struct GizmoAxisScreenSegment {
    GizmoAxis axis;
    glm::vec2 screenFrom;
    glm::vec2 screenTo;
};

// Which axis (if any) is within pickRadiusPixels of screenPoint, given the
// gizmo's three arrows already projected to screen space. Picks the
// nearest if more than one is within range.
std::optional<GizmoAxis> pickGizmoAxis(const std::vector<GizmoAxisScreenSegment>& segments,
                                        glm::vec2 screenPoint, float pickRadiusPixels);
