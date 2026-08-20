#include "render/MeshShader.h"

#include <cstdio>
#include <cstdlib>

namespace {

const char* kVertexShaderSrc = R"(
#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aColor;
layout(location = 3) in float aSelected;

uniform mat4 uMvp;

out vec3 vNormal;
out vec3 vColor;
out float vSelected;
out vec3 vWorldPos;

void main() {
    gl_Position = uMvp * vec4(aPosition, 1.0);
    vNormal = aNormal;
    vColor = aColor;
    vSelected = aSelected;
    vWorldPos = aPosition; // no separate model matrix anywhere in this app -- aPosition IS world space already (applyTransform() ran at mesh-build time)
}
)";

const char* kFragmentShaderSrc = R"(
#version 330 core
#define MAX_LIGHTS 4
#define SELECTION_STYLE_OUTLINE 0
#define SELECTION_STYLE_PULSE 1
#define SELECTION_STYLE_STRIPES 2
#define SELECTION_STYLE_WIREFRAME 3

in vec3 vNormal;
in vec3 vColor;
in float vSelected;
in vec3 vWorldPos;
out vec4 FragColor;

uniform vec3 uLightDirs[MAX_LIGHTS];
uniform vec3 uLightColors[MAX_LIGHTS];
uniform int uLightCount;
uniform int uSelectionStyle;
uniform float uTime;
uniform int uHasSelection;

void main() {
    // Wireframe is drawn as a separate pass on the outline mesh, whose
    // vertices already carry the flat highlight color -- just output it
    // directly, no lighting or per-vertex selection logic needed.
    if (uSelectionStyle == SELECTION_STYLE_WIREFRAME) {
        FragColor = vec4(vColor, 1.0);
        return;
    }

    vec3 baseColor = vColor;
    // When true, skip the lighting multiply below for this fragment --
    // selected-highlight colors should read as a constant glow, not go
    // half-dark on faces angled away from every light (which is what an
    // earlier Pulse-only version did: the highlight was applied to
    // baseColor, but baseColor * lightSum still let N.L darken it face by
    // face, so half the selected tube visibly pulsed and half didn't).
    bool emissive = false;

    if (uHasSelection != 0 && vSelected > 0.5 &&
        (uSelectionStyle == SELECTION_STYLE_PULSE || uSelectionStyle == SELECTION_STYLE_STRIPES)) {
        if (uSelectionStyle == SELECTION_STYLE_PULSE) {
            float pulse = 0.5 + 0.5 * sin(uTime * 3.0);
            baseColor = mix(vColor, vec3(1.0), 0.35 + 0.45 * pulse);
        } else {
            // Diagonal hazard-tape stripes, scrolling along world position
            // (not screen position) so the motion reads the same from any
            // viewing angle -- an unmistakable moving texture rather than
            // just a different flat color.
            float stripeCoord = (vWorldPos.x + vWorldPos.y + vWorldPos.z) * 0.18 - uTime * 3.0;
            float stripe = mod(floor(stripeCoord), 2.0);
            baseColor = stripe > 0.5 ? vec3(1.0) : vec3(0.0);
        }
        emissive = true;
    } else if (uHasSelection != 0 && (uSelectionStyle == SELECTION_STYLE_PULSE || uSelectionStyle == SELECTION_STYLE_STRIPES)) {
        baseColor = vColor * 0.25; // dim everything else so the highlighted geometry pops
    }

    vec3 n = normalize(vNormal);
    vec3 lightSum = vec3(0.35); // ambient floor, same role as the old brightness formula's constant term
    for (int i = 0; i < uLightCount; ++i) {
        float diffuse = max(dot(n, normalize(uLightDirs[i])), 0.0);
        lightSum += uLightColors[i] * diffuse * 0.65;
    }
    vec3 finalColor = emissive ? baseColor : baseColor * lightSum;
    FragColor = vec4(finalColor, 1.0);
}
)";

GLuint compileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::fprintf(stderr, "Mesh shader compile error: %s\n", log);
        std::exit(1);
    }
    return shader;
}

GLuint linkProgram(GLuint vertexShader, GLuint fragmentShader) {
    GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    GLint success = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char log[512];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        std::fprintf(stderr, "Mesh shader link error: %s\n", log);
        std::exit(1);
    }
    return program;
}

} // namespace

GLuint createMeshShaderProgram() {
    GLuint vertexShader = compileShader(GL_VERTEX_SHADER, kVertexShaderSrc);
    GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, kFragmentShaderSrc);
    GLuint program = linkProgram(vertexShader, fragmentShader);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    return program;
}
