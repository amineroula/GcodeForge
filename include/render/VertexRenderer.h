#pragma once

#include "model/Scene.h"
#include "render/LineShader.h"

#include <GL/glew.h>
#include <glm/glm.hpp>

// Draws a point at every path endpoint -- the actual vertices of the
// program. Useful for seeing where one motion command ends and the next
// begins, which is otherwise invisible on a continuous-looking polyline:
// a long straight run and a run made of twenty short collinear moves look
// identical until you can see the vertices. Also how you confirm a path
// split actually inserted a point where you expected.
//
// Selected paths' vertices are drawn in the highlight colour so a
// selection reads clearly even in Lines mode at a distance.
class VertexRenderer {
public:
    VertexRenderer();
    ~VertexRenderer();

    VertexRenderer(const VertexRenderer&) = delete;
    VertexRenderer& operator=(const VertexRenderer&) = delete;

    void rebuild(const Scene& scene, bool includePrintPaths, bool includeTravels);
    void draw(const glm::mat4& viewProj, float pointSizePixels) const;

    size_t vertexCount() const { return static_cast<size_t>(vertexCount_); }

private:
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint shaderProgram_ = 0;
    GLint mvpLoc_ = -1;
    GLsizei vertexCount_ = 0;
    GLsizei vboCapacityVertices_ = 0;
};
