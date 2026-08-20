#include "render/SelectionHighlightRenderer.h"
#include "render/PathColorizer.h"

#include <glm/gtc/type_ptr.hpp>
#include <vector>

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

void SelectionHighlightRenderer::rebuild(const Scene& scene, RenderMode mode) {
    std::vector<LineVertex> vertices;

    for (const auto& object : scene.objects) {
        if (!object.visible || object.selectedPaths.empty()) continue;
        for (const auto& path : object.paths) {
            if (!object.selectedPaths.count(path.number)) continue;
            // Print paths in Geometry mode are handled by GeometryRenderer's
            // own inverted-hull outline mesh instead -- see this class's
            // header comment for why a centerline highlight doesn't work
            // for a path embedded in solid 3D geometry.
            if (mode == RenderMode::Geometry && path.type == PathType::Print) continue;

            glm::vec3 fromWorld(applyTransform(object.transform, path.from));
            glm::vec3 toWorld(applyTransform(object.transform, path.to));
            glm::vec3 color = selectionHighlightColor();
            vertices.push_back({fromWorld, color});
            vertices.push_back({toWorld, color});
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
    // Wide on purpose -- this line is drawn BEFORE the normal scene (see
    // main.cpp), so only the part of it that pokes out beyond the real
    // geometry's own screen-space footprint stays visible once the real
    // geometry draws over it, which is what produces the outline/border
    // look instead of a solid recolor. Wide lines aren't guaranteed >1px
    // on every GPU/driver in core profile (NVIDIA honors it, some don't);
    // the bright color alone still reads as *something* selected either way.
    glLineWidth(6.0f);
    glBindVertexArray(vao_);
    glDrawArrays(GL_LINES, 0, vertexCount_);
    glBindVertexArray(0);
    glLineWidth(1.0f);
}
