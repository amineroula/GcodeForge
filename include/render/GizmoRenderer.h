#pragma once

#include "render/LineShader.h"

#include <GL/glew.h>
#include <glm/glm.hpp>

// Draws the translate gizmo: three colored arrows (X=red, Y=green, Z=blue,
// the same convention as the axis gizmo at the world origin) at a given
// world-space origin -- the active object's pivot. Fixed world-space
// length rather than constant screen-size; consistent with how the origin
// axis gizmo already behaves.
class GizmoRenderer {
public:
    GizmoRenderer();
    ~GizmoRenderer();

    GizmoRenderer(const GizmoRenderer&) = delete;
    GizmoRenderer& operator=(const GizmoRenderer&) = delete;

    void rebuild(const glm::vec3& origin);
    void draw(const glm::mat4& viewProj) const;

    static constexpr float kAxisLengthMm = 200.0f;

private:
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint shaderProgram_ = 0;
    GLint mvpUniformLocation_ = -1;
    GLsizei vertexCount_ = 0;
};
