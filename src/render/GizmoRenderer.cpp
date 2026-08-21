#include "render/GizmoRenderer.h"

#include <glm/gtc/type_ptr.hpp>
#include <cmath>
#include <vector>

namespace {

const glm::vec3 kRed(1.0f, 0.2f, 0.2f);
const glm::vec3 kGreen(0.2f, 1.0f, 0.2f);
const glm::vec3 kBlue(0.25f, 0.5f, 1.0f);
const glm::vec3 kRingColor(1.0f, 0.85f, 0.15f); // amber, distinct from all three axis colors -- reads as "this is the rotate control", not a fourth axis

void appendArrow(std::vector<LineVertex>& vertices, const glm::vec3& origin, const glm::vec3& axisDir,
                  const glm::vec3& headPerp, const glm::vec3& color, float length) {
    glm::vec3 tip = origin + axisDir * length;
    vertices.push_back({origin, color});
    vertices.push_back({tip, color});

    float headLength = length * 0.1f;
    float headWidth = length * 0.04f;
    glm::vec3 headBase = tip - axisDir * headLength;
    vertices.push_back({tip, color});
    vertices.push_back({headBase + headPerp * headWidth, color});
    vertices.push_back({tip, color});
    vertices.push_back({headBase - headPerp * headWidth, color});
}

std::vector<LineVertex> buildMoveVertices(const glm::vec3& origin, float length) {
    std::vector<LineVertex> vertices;
    appendArrow(vertices, origin, glm::vec3(1, 0, 0), glm::vec3(0, 1, 0), kRed, length);
    appendArrow(vertices, origin, glm::vec3(0, 1, 0), glm::vec3(0, 0, 1), kGreen, length);
    appendArrow(vertices, origin, glm::vec3(0, 0, 1), glm::vec3(1, 0, 0), kBlue, length);
    return vertices;
}

std::vector<LineVertex> buildRotateVertices(const glm::vec3& origin, float radius) {
    std::vector<LineVertex> vertices;
    // A ring in the XY plane -- this app's ONLY rotation axis is Z (see
    // GizmoInteractionMode's doc comment), so one ring is the whole
    // rotate gizmo, not one of three.
    for (int i = 0; i < GizmoRenderer::kRingSegmentCount; ++i) {
        float a0 = (static_cast<float>(i) / GizmoRenderer::kRingSegmentCount) * 6.28318530718f;
        float a1 = (static_cast<float>(i + 1) / GizmoRenderer::kRingSegmentCount) * 6.28318530718f;
        glm::vec3 p0 = origin + glm::vec3(std::cos(a0), std::sin(a0), 0.0f) * radius;
        glm::vec3 p1 = origin + glm::vec3(std::cos(a1), std::sin(a1), 0.0f) * radius;
        vertices.push_back({p0, kRingColor});
        vertices.push_back({p1, kRingColor});
    }
    return vertices;
}

} // namespace

GizmoRenderer::GizmoRenderer() {
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
}

GizmoRenderer::~GizmoRenderer() {
    glDeleteBuffers(1, &vbo_);
    glDeleteVertexArrays(1, &vao_);
    glDeleteProgram(shaderProgram_);
}

void GizmoRenderer::rebuild(const glm::vec3& origin, float armLength, GizmoInteractionMode mode) {
    std::vector<LineVertex> vertices = (mode == GizmoInteractionMode::Move)
                                            ? buildMoveVertices(origin, armLength)
                                            : buildRotateVertices(origin, armLength);
    vertexCount_ = static_cast<GLsizei>(vertices.size());

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(LineVertex)), vertices.data(), GL_DYNAMIC_DRAW);
    glBindVertexArray(0);
}

void GizmoRenderer::draw(const glm::mat4& viewProj) const {
    if (vertexCount_ == 0) return;
    glUseProgram(shaderProgram_);
    glUniformMatrix4fv(mvpUniformLocation_, 1, GL_FALSE, glm::value_ptr(viewProj));
    glDisable(GL_DEPTH_TEST); // gizmo should always be visible, drawn on top of everything
    glLineWidth(5.0f);
    glBindVertexArray(vao_);
    glDrawArrays(GL_LINES, 0, vertexCount_);
    glBindVertexArray(0);
    glLineWidth(1.0f);
    glEnable(GL_DEPTH_TEST);
}
