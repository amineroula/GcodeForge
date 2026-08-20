#pragma once

#include "model/Scene.h"

#include <glm/glm.hpp>
#include <optional>

struct FrameBounds {
    glm::vec3 center{0.0f};
    float radius = 1.0f; // never zero -- a degenerate single-point selection still needs a sane framing distance
};

// World-space bounds to point the camera at for "frame selection" (F).
// preferSelection=true computes bounds from ONLY the currently selected
// paths (across all objects) if any exist; otherwise -- and always when
// preferSelection=false -- falls back to every visible object's paths
// ("frame all"), matching the standard F-key behavior in most 3D tools.
// Returns nullopt only if there's nothing at all to frame (empty scene).
std::optional<FrameBounds> computeFrameBounds(const Scene& scene, bool preferSelection);
