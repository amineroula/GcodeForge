#pragma once

#include "model/Scene.h"
#include "render/LineShader.h"
#include "render/RenderSettings.h"

#include <GL/glew.h>
#include <glm/glm.hpp>

// Draws every currently-selected path's CENTERLINE as a wide highlight
// line, drawn BEFORE the normal scene/geometry pass (see main.cpp) so
// normal depth testing does the compositing: at a shared depth, the
// second draw loses ties (GL_LESS, not GL_LEQUAL), so for LINE geometry
// (no volume, same 3D position both times) the highlight simply stays
// fully visible -- an effective solid recolor, which is the right outcome
// for a 1D line (there's no "inside" vs. "border" for a line).
//
// Print paths in GEOMETRY mode are the one case this DOESN'T handle: the
// centerline sits inside a 3D volume, and a fixed-pixel-width line only
// pokes out asymmetrically depending on viewing angle (confirmed --
// reported as "the highlight is only from one side, not the whole line").
// GeometryRenderer handles those instead, with a proper angle-independent
// inverted-hull outline mesh. So rebuild() takes the active RenderMode and
// excludes print paths when it's Geometry, leaving this renderer
// responsible for: everything in Lines mode, and travel paths (still
// plain thin lines with no volume) in either mode.
class SelectionHighlightRenderer {
public:
    SelectionHighlightRenderer();
    ~SelectionHighlightRenderer();

    SelectionHighlightRenderer(const SelectionHighlightRenderer&) = delete;
    SelectionHighlightRenderer& operator=(const SelectionHighlightRenderer&) = delete;

    void rebuild(const Scene& scene, RenderMode mode);
    void draw(const glm::mat4& viewProj) const;

private:
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint shaderProgram_ = 0;
    GLint mvpUniformLocation_ = -1;
    GLsizei vertexCount_ = 0;
    GLsizei vboCapacityVertices_ = 0;
};
