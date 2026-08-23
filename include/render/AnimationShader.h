#pragma once

#include <GL/glew.h>
#include <glm/glm.hpp>

// Shader for the print-animation reveal mesh: same simple Lambertian
// shading as MeshShader, plus ONE extra thing -- each vertex carries the
// cumulative print-order distance (in mm) at which its segment starts
// printing. A single uniform, uRevealDistanceMm, is updated once per
// frame during playback/scrubbing; the fragment shader discards anything
// whose segment hasn't been "printed" yet. This is the whole point of
// the design: revealing geometry costs ONE uniform update per frame, not
// a mesh rebuild, so scrubbing a real 30k+ path file stays smooth.
struct AnimationVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec3 color;
    float revealAtMm = 0.0f;
};

// Returns the linked program. Caller owns it (glDeleteProgram when done).
// Uniforms: "uMvp" (mat4); "uLightDirs"/"uLightColors" (vec3[4]);
// "uLightCount" (int); "uRevealDistanceMm" (float, cumulative distance
// printed so far -- a segment whose own revealAtMm exceeds this is not
// drawn yet).
GLuint createAnimationShaderProgram();
