#pragma once

#include <glm/glm.hpp>
#include <cmath>

// An object's placement on the print bed: translate in X/Y/Z, rotate around
// Z only, optionally flip X or Y. Matches the original's object.transform
// exactly (see docs/PLAN.md) -- it is NOT a general 3D transform, and that's
// deliberate: robotic print objects sit on a flat bed and only ever need to
// be repositioned and spun around the vertical axis, not tumbled in 3D.
struct Transform {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double rotZDegrees = 0.0;
    bool flipX = false;
    bool flipY = false;
};

// Applies the transform to a point in the object's local space, producing
// its world-space position. Order matches the original's
// transformLocalPoint(): flip first, then rotate around Z, then translate.
inline glm::dvec3 applyTransform(const Transform& t, const glm::dvec3& local) {
    double x = local.x * (t.flipX ? -1.0 : 1.0);
    double y = local.y * (t.flipY ? -1.0 : 1.0);

    const double radians = t.rotZDegrees * 3.14159265358979323846 / 180.0;
    const double c = std::cos(radians);
    const double s = std::sin(radians);
    const double xr = x * c - y * s;
    const double yr = x * s + y * c;

    return glm::dvec3(xr + t.x, yr + t.y, local.z + t.z);
}

// Converts a world-space DELTA (a direction/offset, not a point -- so
// translation doesn't apply) back into the object's local space. This is
// the inverse of applyTransform()'s rotation+flip only. Needed when a
// world-space drag (e.g. the move gizmo, which operates in world space)
// needs to be applied to Path::from/to, which are stored in local space:
// worldDelta -> inverseTransformDelta(t, worldDelta) -> localDelta.
inline glm::dvec3 inverseTransformDelta(const Transform& t, const glm::dvec3& worldDelta) {
    const double radians = -t.rotZDegrees * 3.14159265358979323846 / 180.0;
    const double c = std::cos(radians);
    const double s = std::sin(radians);
    const double xr = worldDelta.x * c - worldDelta.y * s;
    const double yr = worldDelta.x * s + worldDelta.y * c;

    const double x = xr * (t.flipX ? -1.0 : 1.0);
    const double y = yr * (t.flipY ? -1.0 : 1.0);

    return glm::dvec3(x, y, worldDelta.z);
}
