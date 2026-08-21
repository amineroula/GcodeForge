#pragma once

#include "model/Scene.h"
#include "render/LineShader.h"

#include <GL/glew.h>
#include <glm/glm.hpp>

// Draws a marker at each visible object's StartPoint (the joint-space
// "first safe position" -- see model/StartPoint.h). Rendered as a 3D
// crosshair plus a small box, in a distinct color, so it reads as a
// reference marker rather than as printable geometry.
//
// Fixed WORLD-SIZE, not screen-size: the operator's actual question is
// "is this point still inside the bed after I moved the object," which is
// a question about world space -- a marker that stayed a constant number
// of pixels while zooming would make a point far outside the bed look
// close to it.
class StartPointRenderer {
public:
    StartPointRenderer();
    ~StartPointRenderer();

    StartPointRenderer(const StartPointRenderer&) = delete;
    StartPointRenderer& operator=(const StartPointRenderer&) = delete;

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
