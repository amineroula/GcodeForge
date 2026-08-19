#include "render/GridRenderer.h"

#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <cstdio>
#include <cstdlib>

namespace {

// Each vertex is 6 floats: position.xyz, color.rgb. GL_LINES draws a
// separate segment for every pair of vertices we push.
struct Vertex {
    glm::vec3 position;
    glm::vec3 color;
};

const char* kVertexShaderSrc = R"(
#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aColor;

uniform mat4 uMvp;

out vec3 vColor;

void main() {
    gl_Position = uMvp * vec4(aPosition, 1.0);
    vColor = aColor;
}
)";

const char* kFragmentShaderSrc = R"(
#version 330 core
in vec3 vColor;
out vec4 FragColor;

void main() {
    FragColor = vec4(vColor, 1.0);
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
        std::fprintf(stderr, "Shader compile error: %s\n", log);
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
        std::fprintf(stderr, "Shader link error: %s\n", log);
        std::exit(1);
    }
    return program;
}

std::vector<Vertex> buildGridVertices() {
    std::vector<Vertex> vertices;

    constexpr float kExtentMm = 500.0f;
    constexpr float kSpacingMm = 50.0f;
    const glm::vec3 gridColor(0.30f, 0.32f, 0.35f);

    for (float i = -kExtentMm; i <= kExtentMm + 0.001f; i += kSpacingMm) {
        vertices.push_back({glm::vec3(i, -kExtentMm, 0.0f), gridColor});
        vertices.push_back({glm::vec3(i, kExtentMm, 0.0f), gridColor});
        vertices.push_back({glm::vec3(-kExtentMm, i, 0.0f), gridColor});
        vertices.push_back({glm::vec3(kExtentMm, i, 0.0f), gridColor});
    }

    // Axis gizmo at the origin: X=red, Y=green, Z=blue (a very common
    // convention in 3D tools — worth memorizing, every DCC app uses it).
    constexpr float kAxisLengthMm = 150.0f;
    vertices.push_back({glm::vec3(0.0f), glm::vec3(1.0f, 0.15f, 0.15f)});
    vertices.push_back({glm::vec3(kAxisLengthMm, 0.0f, 0.0f), glm::vec3(1.0f, 0.15f, 0.15f)});
    vertices.push_back({glm::vec3(0.0f), glm::vec3(0.15f, 1.0f, 0.15f)});
    vertices.push_back({glm::vec3(0.0f, kAxisLengthMm, 0.0f), glm::vec3(0.15f, 1.0f, 0.15f)});
    vertices.push_back({glm::vec3(0.0f), glm::vec3(0.15f, 0.45f, 1.0f)});
    vertices.push_back({glm::vec3(0.0f, 0.0f, kAxisLengthMm), glm::vec3(0.15f, 0.45f, 1.0f)});

    return vertices;
}

} // namespace

GridRenderer::GridRenderer() {
    GLuint vertexShader = compileShader(GL_VERTEX_SHADER, kVertexShaderSrc);
    GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, kFragmentShaderSrc);
    shaderProgram_ = linkProgram(vertexShader, fragmentShader);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    mvpUniformLocation_ = glGetUniformLocation(shaderProgram_, "uMvp");

    std::vector<Vertex> vertices = buildGridVertices();
    vertexCount_ = static_cast<GLsizei>(vertices.size());

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)), vertices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, color)));

    glBindVertexArray(0);
}

GridRenderer::~GridRenderer() {
    glDeleteBuffers(1, &vbo_);
    glDeleteVertexArrays(1, &vao_);
    glDeleteProgram(shaderProgram_);
}

void GridRenderer::draw(const glm::mat4& viewProj) const {
    glUseProgram(shaderProgram_);
    glUniformMatrix4fv(mvpUniformLocation_, 1, GL_FALSE, glm::value_ptr(viewProj));
    glBindVertexArray(vao_);
    glDrawArrays(GL_LINES, 0, vertexCount_);
    glBindVertexArray(0);
}
