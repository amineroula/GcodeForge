#pragma once

#include "editor/Gizmo.h"
#include "render/LineShader.h"

#include <GL/glew.h>
#include <glm/glm.hpp>

// Draws the active gizmo at a given world-space origin: three colored
// arrows (X=red, Y=green, Z=blue, matching the axis gizmo at the world
// origin) in Move mode, or a single ring in the XY plane (Z-axis
// rotation only -- see GizmoInteractionMode's doc comment for why not a
// full 3-axis ball) in Rotate mode.
//
// Size is passed in by the caller as a world-space length, NOT a fixed
// constant -- see Camera::gizmoWorldRadius(), which computes that length
// so the gizmo occupies a constant fraction of the viewport regardless of
// zoom or distance ("always half size" as requested). A truly fixed
// world size would shrink to invisible when zoomed out or dwarf the
// model zoomed in close.
class GizmoRenderer {
public:
    GizmoRenderer();
    ~GizmoRenderer();

    GizmoRenderer(const GizmoRenderer&) = delete;
    GizmoRenderer& operator=(const GizmoRenderer&) = delete;

    void rebuild(const glm::vec3& origin, float armLength, GizmoInteractionMode mode);
    void draw(const glm::mat4& viewProj) const;

    // Number of straight segments approximating the rotate ring's circle
    // -- exposed so picking code (main.cpp) builds screen-space geometry
    // that matches what's actually drawn.
    static constexpr int kRingSegmentCount = 64;

private:
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint shaderProgram_ = 0;
    GLint mvpUniformLocation_ = -1;
    GLsizei vertexCount_ = 0;
};
