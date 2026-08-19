#pragma once

#include <glm/glm.hpp>
#include <GL/glew.h>

// Draws a reference grid on the world XY plane (the "bed") plus a small
// XYZ axis gizmo at the origin. This exists purely so the camera has
// something to look at while we build it — the real toolpath renderer
// comes later (milestone 6).
class GridRenderer {
public:
    GridRenderer();
    ~GridRenderer();

    GridRenderer(const GridRenderer&) = delete;
    GridRenderer& operator=(const GridRenderer&) = delete;

    void draw(const glm::mat4& viewProj) const;

private:
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint shaderProgram_ = 0;
    GLint mvpUniformLocation_ = -1;
    GLsizei vertexCount_ = 0;
};
