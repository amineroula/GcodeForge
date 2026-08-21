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

// Whether the move gizmo (arrows) or the rotate gizmo (a single ring, Z
// axis only) is active. Toggled by the R key. Deliberately only ONE
// rotation axis, not a 3ds-Max-style 3-ring ball: every rotation in this
// app -- Transform::rotZDegrees, rotateSelectedPaths() -- is Z-only by
// design (robotic print objects sit on a flat bed and spin around the
// vertical axis, they don't tumble), so a full 3-axis rotate gizmo would
// offer two axes that do nothing.
enum class GizmoInteractionMode { Move, Rotate };

// Screen-space angle (radians, atan2 convention) from `origin` to
// `screenPoint`, in a coordinate frame where the gizmo's ring actually
// lies -- used to turn cursor movement during a rotate-ring drag into an
// angle delta. `originScreen` is the gizmo's own projected screen
// position (the ring's visual center).
float angleAroundScreenPoint(glm::vec2 originScreen, glm::vec2 screenPoint);

// Is screenPoint within pickRadiusPixels of the ring's outline (a circle
// of `screenRadiusPixels` around originScreen)? Picking the RING (an
// annulus near the circle's edge), not its filled interior, matches how
// every DCC tool's rotate ring works -- you grab the rim, not the middle.
bool pickGizmoRing(glm::vec2 originScreen, float screenRadiusPixels, glm::vec2 screenPoint, float pickRadiusPixels);

// Rotates one world-space point by `deltaDegrees` around Z, pivoting on
// `pivot`. The primitive both rotateObjectAroundPivot() and the gizmo's
// per-path rotate drag (main.cpp) build on.
glm::dvec3 rotatePointAroundPivotZ(const glm::dvec3& point, const glm::dvec3& pivot, double deltaDegrees);

// Rotates a whole object's Transform by `deltaDegrees` around Z, PIVOTING
// on `pivotWorld` rather than the object's own origin -- so the geometry
// visibly spins in place around the gizmo (which sits at the geometry's
// centroid, see computeGizmoOrigin) instead of swinging around wherever
// Transform.x/y/z happens to be. Closed-form: composing the existing
// rotation with an additional world-space rotation-about-a-point only
// requires adding to rotZDegrees and re-deriving the translation that
// keeps the pivot point fixed under the combined transform.
void rotateObjectAroundPivot(Transform& transform, const glm::dvec3& pivotWorld, double deltaDegrees);

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
