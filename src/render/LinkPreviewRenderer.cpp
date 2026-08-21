#include "render/LinkPreviewRenderer.h"
#include "editor/ObjectLinking.h"

#include <glm/gtc/type_ptr.hpp>
#include <vector>

namespace {
const glm::vec3 kLinkPreviewColor(1.0f, 0.4f, 0.9f); // bright magenta -- outside the normal palette on purpose
}

LinkPreviewRenderer::LinkPreviewRenderer() {
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

LinkPreviewRenderer::~LinkPreviewRenderer() {
    glDeleteBuffers(1, &vbo_);
    glDeleteVertexArrays(1, &vao_);
    glDeleteProgram(shaderProgram_);
}

void LinkPreviewRenderer::rebuild(const Scene& scene) {
    std::vector<LineVertex> vertices;
    for (const auto& preview : computeLinkPreviews(scene)) {
        vertices.push_back({glm::vec3(preview.worldFrom), kLinkPreviewColor});
        vertices.push_back({glm::vec3(preview.worldTo), kLinkPreviewColor});
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

void LinkPreviewRenderer::draw(const glm::mat4& viewProj) const {
    if (vertexCount_ == 0) return;
    glUseProgram(shaderProgram_);
    glUniformMatrix4fv(mvpLoc_, 1, GL_FALSE, glm::value_ptr(viewProj));
    glBindVertexArray(vao_);
    glDrawArrays(GL_LINES, 0, vertexCount_);
    glBindVertexArray(0);
}
