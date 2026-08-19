#pragma once

#include <GL/glew.h>
#include <glm/glm.hpp>

// Shared shader for GeometryRenderer's "bead" boxes: position + normal +
// color, with simple fixed-direction Lambertian shading so solid geometry
// actually reads as a 3D volume instead of a flat silhouette. Deliberately
// minimal -- one light, no shadows -- this is a toolpath preview, not a
// physically-based renderer.
struct MeshVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec3 color;
};

// Returns the linked program. Caller owns it (glDeleteProgram when done).
// Uniforms: "uMvp" (mat4), "uLightDir" (vec3, world-space, pointing FROM
// the surface TOWARD the light).
GLuint createMeshShaderProgram();
