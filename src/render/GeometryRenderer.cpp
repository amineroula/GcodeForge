#include "render/GeometryRenderer.h"

#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <cstdint>

namespace {

// Local vertex indices within one box (0-7), forming 12 triangles (6
// faces). Winding isn't consistent outward-facing on every face -- face
// culling is left disabled (see GeometryRenderer::draw), so it doesn't
// matter for correctness, only normals matter for shading.
constexpr uint32_t kBoxTriangleIndices[36] = {
    0, 1, 5,  0, 5, 4,  // bottom
    3, 2, 6,  3, 6, 7,  // top
    1, 2, 6,  1, 6, 5,  // +right side
    0, 3, 7,  0, 7, 4,  // -right side
    0, 1, 2,  0, 2, 3,  // start cap
    4, 5, 6,  4, 6, 7,  // end cap
};

// Appends one bead box for a print path segment. Cross-section is a
// widthxheight rectangle centered on the from->to centerline; height is
// always along world Z (the bead sits "flat," regardless of the segment's
// own tilt) -- a deliberate simplification, matches how these viewers
// conventionally show layer height regardless of local path angle.
void appendBox(std::vector<MeshVertex>& vertices, std::vector<uint32_t>& indices,
                const glm::vec3& from, const glm::vec3& to, float halfWidth, float halfHeight,
                const glm::vec3& color) {
    glm::vec3 dir = to - from;
    float length = glm::length(dir);
    glm::vec3 dirUnit = (length > 1e-6f) ? dir / length : glm::vec3(1.0f, 0.0f, 0.0f);

    const glm::vec3 worldUp(0.0f, 0.0f, 1.0f);
    glm::vec3 rightUnit = glm::cross(dirUnit, worldUp);
    if (glm::length(rightUnit) < 1e-4f) rightUnit = glm::vec3(1.0f, 0.0f, 0.0f); // segment is vertical
    else rightUnit = glm::normalize(rightUnit);

    glm::vec3 right = rightUnit * halfWidth;
    glm::vec3 up = worldUp * halfHeight;

    // Corner layout: [end: from/to][rightSign][upSign], matching the
    // 0-7 indices kBoxTriangleIndices expects.
    glm::vec3 positions[8] = {
        from - right - up, from + right - up, from + right + up, from - right + up,
        to - right - up,   to + right - up,   to + right + up,   to - right + up,
    };
    // Normal depends only on which corner of the cross-section this is,
    // not on which end (from/to) -- see the class comment on why.
    glm::vec3 cornerNormals[4] = {
        glm::normalize(-rightUnit - worldUp),
        glm::normalize(rightUnit - worldUp),
        glm::normalize(rightUnit + worldUp),
        glm::normalize(-rightUnit + worldUp),
    };

    uint32_t base = static_cast<uint32_t>(vertices.size());
    for (int end = 0; end < 2; ++end) {
        for (int corner = 0; corner < 4; ++corner) {
            vertices.push_back({positions[end * 4 + corner], cornerNormals[corner], color});
        }
    }
    for (uint32_t localIndex : kBoxTriangleIndices) {
        indices.push_back(base + localIndex);
    }
}

} // namespace

GeometryRenderer::GeometryRenderer() {
    meshShaderProgram_ = createMeshShaderProgram();
    meshMvpLoc_ = glGetUniformLocation(meshShaderProgram_, "uMvp");
    meshLightDirLoc_ = glGetUniformLocation(meshShaderProgram_, "uLightDir");

    glGenVertexArrays(1, &meshVao_);
    glGenBuffers(1, &meshVbo_);
    glGenBuffers(1, &meshEbo_);
    glBindVertexArray(meshVao_);
    glBindBuffer(GL_ARRAY_BUFFER, meshVbo_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, meshEbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), reinterpret_cast<void*>(offsetof(MeshVertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), reinterpret_cast<void*>(offsetof(MeshVertex, normal)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), reinterpret_cast<void*>(offsetof(MeshVertex, color)));
    glBindVertexArray(0);

    travelShaderProgram_ = createLineShaderProgram();
    travelMvpLoc_ = glGetUniformLocation(travelShaderProgram_, "uMvp");

    glGenVertexArrays(1, &travelVao_);
    glGenBuffers(1, &travelVbo_);
    glBindVertexArray(travelVao_);
    glBindBuffer(GL_ARRAY_BUFFER, travelVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(LineVertex), reinterpret_cast<void*>(offsetof(LineVertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(LineVertex), reinterpret_cast<void*>(offsetof(LineVertex, color)));
    glBindVertexArray(0);
}

GeometryRenderer::~GeometryRenderer() {
    glDeleteBuffers(1, &meshVbo_);
    glDeleteBuffers(1, &meshEbo_);
    glDeleteVertexArrays(1, &meshVao_);
    glDeleteProgram(meshShaderProgram_);

    glDeleteBuffers(1, &travelVbo_);
    glDeleteVertexArrays(1, &travelVao_);
    glDeleteProgram(travelShaderProgram_);
}

void GeometryRenderer::rebuild(const Scene& scene, ColorMode colorMode, float beadWidthMm, float beadHeightMm) {
    speedColors_.rebuild(scene.objects);

    std::vector<MeshVertex> meshVertices;
    std::vector<uint32_t> meshIndices;
    std::vector<LineVertex> travelVertices;

    float halfWidth = beadWidthMm * 0.5f;
    float halfHeight = beadHeightMm * 0.5f;

    for (const auto& object : scene.objects) {
        if (!object.visible) continue;
        for (const auto& path : object.paths) {
            glm::vec3 fromWorld(applyTransform(object.transform, path.from));
            glm::vec3 toWorld(applyTransform(object.transform, path.to));
            glm::vec3 color = pathColor(object, path, colorMode, speedColors_);

            if (path.type == PathType::Print) {
                appendBox(meshVertices, meshIndices, fromWorld, toWorld, halfWidth, halfHeight, color);
            } else {
                travelVertices.push_back({fromWorld, color});
                travelVertices.push_back({toWorld, color});
            }
        }
    }

    indexCount_ = static_cast<GLsizei>(meshIndices.size());
    glBindVertexArray(meshVao_);
    glBindBuffer(GL_ARRAY_BUFFER, meshVbo_);
    GLsizei vertexCount = static_cast<GLsizei>(meshVertices.size());
    if (vertexCount > vboCapacityVertices_) {
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(meshVertices.size() * sizeof(MeshVertex)), meshVertices.data(), GL_DYNAMIC_DRAW);
        vboCapacityVertices_ = vertexCount;
    } else if (vertexCount > 0) {
        glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(meshVertices.size() * sizeof(MeshVertex)), meshVertices.data());
    }
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, meshEbo_);
    if (indexCount_ > eboCapacityIndices_) {
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(meshIndices.size() * sizeof(uint32_t)), meshIndices.data(), GL_DYNAMIC_DRAW);
        eboCapacityIndices_ = indexCount_;
    } else if (indexCount_ > 0) {
        glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(meshIndices.size() * sizeof(uint32_t)), meshIndices.data());
    }
    glBindVertexArray(0);

    travelVertexCount_ = static_cast<GLsizei>(travelVertices.size());
    glBindVertexArray(travelVao_);
    glBindBuffer(GL_ARRAY_BUFFER, travelVbo_);
    if (travelVertexCount_ > travelVboCapacityVertices_) {
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(travelVertices.size() * sizeof(LineVertex)), travelVertices.data(), GL_DYNAMIC_DRAW);
        travelVboCapacityVertices_ = travelVertexCount_;
    } else if (travelVertexCount_ > 0) {
        glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(travelVertices.size() * sizeof(LineVertex)), travelVertices.data());
    }
    glBindVertexArray(0);
}

void GeometryRenderer::draw(const glm::mat4& viewProj, const glm::vec3& lightDir) const {
    glDisable(GL_CULL_FACE);

    if (indexCount_ > 0) {
        glUseProgram(meshShaderProgram_);
        glUniformMatrix4fv(meshMvpLoc_, 1, GL_FALSE, glm::value_ptr(viewProj));
        glUniform3fv(meshLightDirLoc_, 1, glm::value_ptr(lightDir));
        glBindVertexArray(meshVao_);
        glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, nullptr);
        glBindVertexArray(0);
    }

    if (travelVertexCount_ > 0) {
        glUseProgram(travelShaderProgram_);
        glUniformMatrix4fv(travelMvpLoc_, 1, GL_FALSE, glm::value_ptr(viewProj));
        glBindVertexArray(travelVao_);
        glDrawArrays(GL_LINES, 0, travelVertexCount_);
        glBindVertexArray(0);
    }
}
