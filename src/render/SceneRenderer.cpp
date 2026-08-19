#include "render/SceneRenderer.h"

#include <glm/gtc/type_ptr.hpp>
#include <vector>

SceneRenderer::SceneRenderer() {
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

SceneRenderer::~SceneRenderer() {
    glDeleteBuffers(1, &vbo_);
    glDeleteVertexArrays(1, &vao_);
    glDeleteProgram(shaderProgram_);
}

void SceneRenderer::rebuild(const Scene& scene, ColorMode colorMode) {
    speedColors_.rebuild(scene.objects);

    std::vector<LineVertex> vertices;
    vertices.reserve(scene.objects.size() * 64);

    for (const auto& object : scene.objects) {
        if (!object.visible) continue;
        for (const auto& path : object.paths) {
            glm::dvec3 fromWorld = applyTransform(object.transform, path.from);
            glm::dvec3 toWorld = applyTransform(object.transform, path.to);
            glm::vec3 color = pathColor(object, path, colorMode, speedColors_);

            vertices.push_back({glm::vec3(fromWorld), color});
            vertices.push_back({glm::vec3(toWorld), color});
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

void SceneRenderer::draw(const glm::mat4& viewProj) const {
    if (vertexCount_ == 0) return;
    glUseProgram(shaderProgram_);
    glUniformMatrix4fv(mvpUniformLocation_, 1, GL_FALSE, glm::value_ptr(viewProj));
    glBindVertexArray(vao_);
    glDrawArrays(GL_LINES, 0, vertexCount_);
    glBindVertexArray(0);
}
