#pragma once

#include "model/Scene.h"
#include "render/LineShader.h"

#include <GL/glew.h>
#include <glm/glm.hpp>

// Draws every currently-selected path's CENTERLINE as a bright overlay
// line. This alone is enough in Lines mode, but NOT in Geometry mode: the
// centerline sits inside the solid bead box, so the depth test correctly
// hides it behind the box's own opaque surface -- a real bug that showed
// up as "selection is invisible in Geometry mode." The actual fix for
// Geometry mode is in GeometryRenderer, which bakes selectionHighlightColor()
// directly into the selected paths' bead/travel-line vertex colors instead
// of relying on this overlay. This renderer is still useful there for
// travel paths (thin lines, nothing occludes them) and is the whole
// mechanism in Lines mode.
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
