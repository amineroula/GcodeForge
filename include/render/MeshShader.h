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
    float selected = 0.0f; // 1.0 if this vertex's path is in the current selection, else 0.0 -- only read by the Pulse selection style
};

// Returns the linked program. Caller owns it (glDeleteProgram when done).
// Uniforms: "uMvp" (mat4); "uLightDirs"/"uLightColors" (vec3[4], world-space
// direction pointing FROM the surface TOWARD each light, and its color);
// "uLightCount" (int, how many of the 4 array slots are actually active);
// "uSelectionStyle" (int, 0=Outline/no per-vertex effect, 1=Pulse);
// "uTime" (float, seconds, drives the Pulse style's animation);
// "uHasSelection" (int/bool, Pulse only dims non-selected geometry when
// something is actually selected -- otherwise every vertex has
// aSelected=0 and the whole scene would incorrectly dim).
GLuint createMeshShaderProgram();
