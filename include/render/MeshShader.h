#pragma once

#include <GL/glew.h>
#include <glm/glm.hpp>

// Shared shader for GeometryRenderer's "bead" boxes: position + normal +
// color, with simple Lambertian shading (multiple directional lights, no
// shadows) so solid geometry actually reads as a 3D volume instead of a
// flat silhouette. Deliberately simple -- this is a toolpath preview, not
// a physically-based renderer.
struct MeshVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec3 color;
};

// Returns the linked program. Caller owns it (glDeleteProgram when done).
// Uniforms: "uMvp" (mat4); "uLightDirs"/"uLightColors" (vec3[4], world-space
// direction pointing FROM the surface TOWARD each light, and its color);
// "uLightCount" (int, how many of the 4 array slots are actually active).
GLuint createMeshShaderProgram();
