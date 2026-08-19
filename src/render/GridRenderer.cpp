#include "render/GridRenderer.h"
#include "render/LineShader.h"

#include <glm/gtc/type_ptr.hpp>
#include <vector>

namespace {

std::vector<LineVertex> buildVertices(const BedSettings& bed) {
    std::vector<LineVertex> vertices;
    const glm::vec3 gridColor(0.30f, 0.32f, 0.35f);

    if (bed.showGrid && bed.gridSpacingMm > 0.01f) {
        float halfWidth = bed.widthMm * 0.5f;
        float halfDepth = bed.depthMm * 0.5f;
        float minX = bed.originXMm - halfWidth, maxX = bed.originXMm + halfWidth;
        float minY = bed.originYMm - halfDepth, maxY = bed.originYMm + halfDepth;

        for (float x = minX; x <= maxX + 0.001f; x += bed.gridSpacingMm) {
            vertices.push_back({glm::vec3(x, minY, bed.originZMm), gridColor});
            vertices.push_back({glm::vec3(x, maxY, bed.originZMm), gridColor});
        }
        for (float y = minY; y <= maxY + 0.001f; y += bed.gridSpacingMm) {
            vertices.push_back({glm::vec3(minX, y, bed.originZMm), gridColor});
            vertices.push_back({glm::vec3(maxX, y, bed.originZMm), gridColor});
        }
    }

    // Axis gizmo at the true world origin: X=red, Y=green, Z=blue (a very
    // common convention in 3D tools — worth memorizing, every DCC app uses
    // it). Deliberately NOT tied to bed.origin* -- it's a fixed reference
    // frame, so it stays put even when the bed itself is repositioned.
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

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(LineVertex), reinterpret_cast<void*>(offsetof(LineVertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(LineVertex), reinterpret_cast<void*>(offsetof(LineVertex, color)));
    glBindVertexArray(0);

    rebuild(BedSettings{});
}

GridRenderer::~GridRenderer() {
    glDeleteBuffers(1, &vbo_);
    glDeleteVertexArrays(1, &vao_);
    glDeleteProgram(shaderProgram_);
}

void GridRenderer::rebuild(const BedSettings& bed) {
    std::vector<LineVertex> vertices = buildVertices(bed);
    vertexCount_ = static_cast<GLsizei>(vertices.size());

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    if (vertexCount_ > vboCapacityVertices_) {
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(LineVertex)), vertices.data(), GL_DYNAMIC_DRAW);
        vboCapacityVertices_ = vertexCount_;
    } else if (vertexCount_ > 0) {
        glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(vertices.size() * sizeof(LineVertex)), vertices.data());
    }
    glBindVertexArray(0);
}

void GridRenderer::draw(const glm::mat4& viewProj) const {
    glUseProgram(shaderProgram_);
    glUniformMatrix4fv(mvpUniformLocation_, 1, GL_FALSE, glm::value_ptr(viewProj));
    glBindVertexArray(vao_);
    glDrawArrays(GL_LINES, 0, vertexCount_);
    glBindVertexArray(0);
}
