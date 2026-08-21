#pragma once

#include "model/Scene.h"
#include "render/LineShader.h"

#include <GL/glew.h>
#include <glm/glm.hpp>

// Draws one line per PENDING (not-yet-baked) entry in scene.objectLinks
// -- see editor/ObjectLinking.h. A distinct bright color (outside the
// normal palette, like SelectionHighlightRenderer's) so it reads clearly
// as "this isn't real path data yet," unlike a real travel move.
class LinkPreviewRenderer {
public:
    LinkPreviewRenderer();
    ~LinkPreviewRenderer();

    LinkPreviewRenderer(const LinkPreviewRenderer&) = delete;
    LinkPreviewRenderer& operator=(const LinkPreviewRenderer&) = delete;

    void rebuild(const Scene& scene);
    void draw(const glm::mat4& viewProj) const;

private:
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint shaderProgram_ = 0;
    GLint mvpLoc_ = -1;
    GLsizei vertexCount_ = 0;
    GLsizei vboCapacityVertices_ = 0;
};
