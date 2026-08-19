#include "render/SelectionHighlightRenderer.h"

#include <glm/gtc/type_ptr.hpp>
#include <vector>

namespace {
const glm::vec3 kHighlightColor(1.0f, 1.0f, 0.15f); // bright yellow -- reads over both print-green and travel-orange
}

SelectionHighlightRenderer::SelectionHighlightRenderer() {
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

SelectionHighlightRenderer::~SelectionHighlightRenderer() {
    glDeleteBuffers(1, &vbo_);
    glDeleteVertexArrays(1, &vao_);
    glDeleteProgram(shaderProgram_);
}

void SelectionHighlightRenderer::rebuild(const Scene& scene) {
    std::vector<LineVertex> vertices;

    for (const auto& object : scene.objects) {
        if (!object.visible || object.selectedPaths.empty()) continue;
        for (const auto& path : object.paths) {
            if (!object.selectedPaths.count(path.number)) continue;
            glm::vec3 fromWorld(applyTransform(object.transform, path.from));
            glm::vec3 toWorld(applyTransform(object.transform, path.to));
            vertices.push_back({fromWorld, kHighlightColor});
            vertices.push_back({toWorld, kHighlightColor});
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

void SelectionHighlightRenderer::draw(const glm::mat4& viewProj) const {
    if (vertexCount_ == 0) return;
    glUseProgram(shaderProgram_);
    glUniformMatrix4fv(mvpUniformLocation_, 1, GL_FALSE, glm::value_ptr(viewProj));
    glLineWidth(3.0f); // wide lines aren't guaranteed >1px on every GPU/driver in core profile (NVIDIA honors it, some don't) -- the bright color alone still reads fine either way
    glBindVertexArray(vao_);
    glDrawArrays(GL_LINES, 0, vertexCount_);
    glBindVertexArray(0);
    glLineWidth(1.0f);
}
