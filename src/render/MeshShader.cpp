#include "render/MeshShader.h"

#include <cstdio>
#include <cstdlib>

namespace {

const char* kVertexShaderSrc = R"(
#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aColor;

uniform mat4 uMvp;

out vec3 vNormal;
out vec3 vColor;

void main() {
    gl_Position = uMvp * vec4(aPosition, 1.0);
    vNormal = aNormal;
    vColor = aColor;
}
)";

const char* kFragmentShaderSrc = R"(
#version 330 core
#define MAX_LIGHTS 4

in vec3 vNormal;
in vec3 vColor;
out vec4 FragColor;

uniform vec3 uLightDirs[MAX_LIGHTS];
uniform vec3 uLightColors[MAX_LIGHTS];
uniform int uLightCount;

void main() {
    vec3 n = normalize(vNormal);
    vec3 lightSum = vec3(0.35); // ambient floor, same role as the old brightness formula's constant term
    for (int i = 0; i < uLightCount; ++i) {
        float diffuse = max(dot(n, normalize(uLightDirs[i])), 0.0);
        lightSum += uLightColors[i] * diffuse * 0.65;
    }
    FragColor = vec4(vColor * lightSum, 1.0);
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
