#include "render/GridRenderer.h"
#include "render/LineShader.h"

#include <glm/gtc/type_ptr.hpp>
#include <vector>

namespace {

std::vector<LineVertex> buildGridVertices() {
    std::vector<LineVertex> vertices;

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
    shaderProgram_ = createLineShaderProgram();
    mvpUniformLocation_ = glGetUniformLocation(shaderProgram_, "uMvp");

    std::vector<LineVertex> vertices = buildGridVertices();
    vertexCount_ = static_cast<GLsizei>(vertices.size());

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(LineVertex)), vertices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(LineVertex), reinterpret_cast<void*>(offsetof(LineVertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(LineVertex), reinterpret_cast<void*>(offsetof(LineVertex, color)));

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
