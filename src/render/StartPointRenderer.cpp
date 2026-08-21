#include "render/StartPointRenderer.h"
#include "model/Transform.h"

#include <glm/gtc/type_ptr.hpp>
#include <vector>

namespace {

const glm::vec3 kStartPointColor(1.0f, 0.85f, 0.1f); // amber -- outside the path palette, reads as "reference marker"
constexpr float kArmLengthMm = 60.0f;                // crosshair half-length
constexpr float kBoxHalfMm = 18.0f;                  // small box around the centre

void appendLine(std::vector<LineVertex>& vertices, const glm::vec3& a, const glm::vec3& b) {
    vertices.push_back({a, kStartPointColor});
    vertices.push_back({b, kStartPointColor});
}

} // namespace

StartPointRenderer::StartPointRenderer() {
    shaderProgram_ = createLineShaderProgram();
    mvpLoc_ = glGetUniformLocation(shaderProgram_, "uMvp");

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

StartPointRenderer::~StartPointRenderer() {
    glDeleteBuffers(1, &vbo_);
    glDeleteVertexArrays(1, &vao_);
    glDeleteProgram(shaderProgram_);
}

void StartPointRenderer::rebuild(const Scene& scene) {
    std::vector<LineVertex> vertices;

    for (const auto& object : scene.objects) {
        if (!object.visible) continue;
        if (!object.startPoint.present || !object.startPoint.position.has_value()) continue;

        // Stored in LOCAL space, so it rides along with the object's own
        // transform exactly like any path does -- which is the whole
        // point: move the object and this marker moves with it, making
        // "did my start point just leave the bed?" answerable by looking.
        glm::vec3 centre(applyTransform(object.transform, *object.startPoint.position));

        appendLine(vertices, centre - glm::vec3(kArmLengthMm, 0.0f, 0.0f), centre + glm::vec3(kArmLengthMm, 0.0f, 0.0f));
        appendLine(vertices, centre - glm::vec3(0.0f, kArmLengthMm, 0.0f), centre + glm::vec3(0.0f, kArmLengthMm, 0.0f));
        appendLine(vertices, centre - glm::vec3(0.0f, 0.0f, kArmLengthMm), centre + glm::vec3(0.0f, 0.0f, kArmLengthMm));

        // A small open box around the centre, so the marker still reads
        // as a distinct object when the crosshair arms happen to lie
        // along a path.
        const float h = kBoxHalfMm;
        glm::vec3 corners[8] = {
            centre + glm::vec3(-h, -h, -h), centre + glm::vec3(h, -h, -h),
            centre + glm::vec3(h, h, -h),   centre + glm::vec3(-h, h, -h),
            centre + glm::vec3(-h, -h, h),  centre + glm::vec3(h, -h, h),
            centre + glm::vec3(h, h, h),    centre + glm::vec3(-h, h, h),
        };
        for (int i = 0; i < 4; ++i) {
            appendLine(vertices, corners[i], corners[(i + 1) % 4]);         // bottom ring
            appendLine(vertices, corners[i + 4], corners[((i + 1) % 4) + 4]); // top ring
            appendLine(vertices, corners[i], corners[i + 4]);               // vertical edge
        }
    }

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

void StartPointRenderer::draw(const glm::mat4& viewProj) const {
    if (vertexCount_ == 0) return;
    glUseProgram(shaderProgram_);
    glUniformMatrix4fv(mvpLoc_, 1, GL_FALSE, glm::value_ptr(viewProj));
    glLineWidth(2.5f);
    glBindVertexArray(vao_);
    glDrawArrays(GL_LINES, 0, vertexCount_);
    glBindVertexArray(0);
    glLineWidth(1.0f);
}
