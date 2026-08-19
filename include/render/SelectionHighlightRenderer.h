#pragma once

#include "model/Scene.h"
#include "render/LineShader.h"

#include <GL/glew.h>
#include <glm/glm.hpp>

// Draws every currently-selected path as a bright overlay line, on top of
// whichever main renderer (Lines or Geometry) is active. Selection needs
// to be visible regardless of render mode or color mode -- a fixed
// highlight color that ignores ColorMode entirely is what makes "what's
// selected" readable no matter what else is going on screen.
class SelectionHighlightRenderer {
public:
    SelectionHighlightRenderer();
    ~SelectionHighlightRenderer();

    SelectionHighlightRenderer(const SelectionHighlightRenderer&) = delete;
    SelectionHighlightRenderer& operator=(const SelectionHighlightRenderer&) = delete;

    void rebuild(const Scene& scene);
    void draw(const glm::mat4& viewProj) const;

private:
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint shaderProgram_ = 0;
    GLint mvpUniformLocation_ = -1;
    GLsizei vertexCount_ = 0;
    GLsizei vboCapacityVertices_ = 0;
};
