#pragma once

#include <GL/glew.h>
#include <glm/glm.hpp>

// The one shader used by every GL_LINES renderer in this app so far
// (GridRenderer, SceneRenderer): position + per-vertex color, transformed
// by a single MVP matrix. Shared here instead of duplicated per renderer.
struct LineVertex {
    glm::vec3 position;
    glm::vec3 color;
};

// Returns the linked program. Caller owns it (glDeleteProgram when done).
GLuint createLineShaderProgram();
