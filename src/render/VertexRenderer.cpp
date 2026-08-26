#include "render/VertexRenderer.h"
#include "model/Transform.h"
#include "render/PathColorizer.h"

#include <glm/gtc/type_ptr.hpp>
#include <vector>

namespace {
const glm::vec3 kVertexColor(0.85f, 0.88f, 0.95f); // near-white: reads against every palette colour
}

VertexRenderer::VertexRenderer() {
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

VertexRenderer::~VertexRenderer() {
    glDeleteBuffers(1, &vbo_);
    glDeleteVertexArrays(1, &vao_);
    glDeleteProgram(shaderProgram_);
}

void VertexRenderer::rebuild(const Scene& scene, bool includePrintPaths, bool includeTravels) {
    std::vector<LineVertex> vertices;

    for (const auto& object : scene.objects) {
        if (!object.visible) continue;
        for (const auto& path : object.paths) {
            if (path.type == PathType::Print && !includePrintPaths) continue;
            if (path.type == PathType::Travel && !includeTravels) continue;
            if (object.hiddenPaths.count(path.number)) continue;

            bool selected = object.selectedPaths.count(path.number) > 0;
            glm::vec3 color = selected ? selectionHighlightColor() : kVertexColor;

            // Only the END point of each path: consecutive connected
            // paths share a vertex, so emitting both ends would draw
            // every interior vertex twice for no visual gain and double
            // the buffer on a 24k-path file. The very first FROM is
            // emitted separately below so the run's start isn't missed.
            vertices.push_back({glm::vec3(applyTransform(object.transform, path.to)), color});
        }
        if (!object.paths.empty()) {
            const Path& first = object.paths.front();
            bool selected = object.selectedPaths.count(first.number) > 0;
            bool include = (first.type == PathType::Print) ? includePrintPaths : includeTravels;
            if (object.hiddenPaths.count(first.number)) include = false;
            if (include) {
                vertices.push_back({glm::vec3(applyTransform(object.transform, first.from)),
                                     selected ? selectionHighlightColor() : kVertexColor});
            }
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

void VertexRenderer::draw(const glm::mat4& viewProj, float pointSizePixels) const {
    if (vertexCount_ == 0) return;
    glUseProgram(shaderProgram_);
    glUniformMatrix4fv(mvpLoc_, 1, GL_FALSE, glm::value_ptr(viewProj));
    glPointSize(pointSizePixels);
    glBindVertexArray(vao_);
    glDrawArrays(GL_POINTS, 0, vertexCount_);
    glBindVertexArray(0);
    glPointSize(1.0f);
}
