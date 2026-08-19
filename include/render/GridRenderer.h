#pragma once

#include "render/BedSettings.h"

#include <glm/glm.hpp>
#include <GL/glew.h>

// Draws the print bed's reference grid (rebuildable from BedSettings --
// size, position, line spacing) plus a small XYZ axis gizmo fixed at the
// true world origin (0,0,0), independent of bed position, so there's
// always a stable frame of reference even when the bed itself is moved.
class GridRenderer {
public:
    GridRenderer();
    ~GridRenderer();

    GridRenderer(const GridRenderer&) = delete;
    GridRenderer& operator=(const GridRenderer&) = delete;

    void rebuild(const BedSettings& bed);
    void draw(const glm::mat4& viewProj) const;

private:
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint shaderProgram_ = 0;
    GLint mvpUniformLocation_ = -1;
    GLsizei vertexCount_ = 0;
    GLsizei vboCapacityVertices_ = 0;
};
