#pragma once

#include <glm/glm.hpp>
#include <vector>

// One directional light: no position, just a direction (world-space,
// pointing FROM the surface TOWARD the light -- matches the original
// single-light convention) and a color. Good enough for a toolpath
// preview; this isn't trying to be a physically-based renderer.
struct Light {
    glm::vec3 direction{0.4f, -0.5f, 0.8f};
    glm::vec3 color{1.0f, 1.0f, 1.0f};
    bool enabled = true;
};

struct LightingSettings {
    static constexpr int kMaxLights = 4; // matches MeshShader's fixed-size uniform array

    // Starts with one light matching the previous hardcoded default, so
    // existing Geometry-mode shading looks the same until the operator
    // actually changes something in the new Environment panel.
    std::vector<Light> lights = {Light{}};
};
