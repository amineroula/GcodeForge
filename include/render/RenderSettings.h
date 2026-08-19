#pragma once

// Which GPU representation the viewport uses for the scene. Both modes
// read the same Scene/ColorMode -- this only changes how it's drawn.
enum class RenderMode {
    Lines,     // infinitely-thin GL_LINES -- cheap, exact path centerlines (SceneRenderer)
    Geometry   // solid rectangular "bead" boxes approximating the deposited
               // material cross-section, print paths only; travel paths
               // still draw as thin lines so nozzle-up moves stay visible
               // (GeometryRenderer)
};

struct RenderSettings {
    RenderMode mode = RenderMode::Lines;
    float beadWidthMm = 8.0f;   // cross-section width of the simulated extrusion bead
    float beadHeightMm = 4.0f;  // cross-section height (roughly: layer height)
};
