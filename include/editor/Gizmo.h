#pragma once

#include "model/SceneObject.h"

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

// What the gizmo edits when dragged. Object moves the whole object
// (mutates Transform.x/y/z); Start/End/Whole edit the CURRENTLY SELECTED
// paths directly (mutates Path::from and/or Path::to in local space) --
// Start only moves each selected path's start point, End only its end
// point (both can break connectivity with an unselected neighboring path,
// same trade-off as moving one vertex of a polyline in any curve editor),
// Whole translates each selected path rigidly (both endpoints together,
// so it never breaks connectivity by itself).
enum class GizmoTargetMode { Object, Start, End, Whole };

// Where the gizmo should be drawn, in world space. Deliberately NOT the
// object's raw Transform.x/y/z pivot -- that can be arbitrarily far from
// the actual geometry (e.g. a freshly-loaded real KUKA file typically has
// Transform == {0,0,0} while its coordinates are in the thousands of mm,
// which made the gizmo render off in empty space, invisible relative to
// where the geometry actually is on screen). Instead this is always the
// world-space centroid of whatever the gizmo would actually move:
//  - Start/End/Whole with a non-empty selection: centroid of the relevant
//    point(s) of the selected paths.
//  - Object mode, or Start/End/Whole with nothing selected (falls back to
//    Object mode): centroid of the WHOLE object's paths.
// Returns nullopt only if the object has no paths at all.
std::optional<glm::vec3> computeGizmoOrigin(const SceneObject& object, GizmoTargetMode mode);
