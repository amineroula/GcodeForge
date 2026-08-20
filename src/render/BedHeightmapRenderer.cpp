#include "render/BedHeightmapRenderer.h"

#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cstdint>
#include <vector>

namespace {

// Blue (low) -> green (mid) -> red (high) heatmap ramp, t in [0, 1].
// Standard "cold to hot" convention -- no legend needed for an operator to
// read "blue = low spot, red = high spot" at a glance.
glm::vec3 heatColor(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    const glm::vec3 kLow(0.15f, 0.35f, 0.95f);
    const glm::vec3 kMid(0.20f, 0.85f, 0.30f);
    const glm::vec3 kHigh(0.95f, 0.20f, 0.15f);
    return (t < 0.5f) ? glm::mix(kLow, kMid, t * 2.0f) : glm::mix(kMid, kHigh, (t - 0.5f) * 2.0f);
}

} // namespace

BedHeightmapRenderer::BedHeightmapRenderer() {
    shaderProgram_ = createMeshShaderProgram();
    mvpLoc_ = glGetUniformLocation(shaderProgram_, "uMvp");
    lightDirsLoc_ = glGetUniformLocation(shaderProgram_, "uLightDirs");
    lightColorsLoc_ = glGetUniformLocation(shaderProgram_, "uLightColors");
    lightCountLoc_ = glGetUniformLocation(shaderProgram_, "uLightCount");
    selectionStyleLoc_ = glGetUniformLocation(shaderProgram_, "uSelectionStyle");

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ebo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), reinterpret_cast<void*>(offsetof(MeshVertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), reinterpret_cast<void*>(offsetof(MeshVertex, normal)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), reinterpret_cast<void*>(offsetof(MeshVertex, color)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), reinterpret_cast<void*>(offsetof(MeshVertex, selected)));
    glBindVertexArray(0);
}

BedHeightmapRenderer::~BedHeightmapRenderer() {
    glDeleteBuffers(1, &vbo_);
    glDeleteBuffers(1, &ebo_);
    glDeleteVertexArrays(1, &vao_);
    glDeleteProgram(shaderProgram_);
}

void BedHeightmapRenderer::rebuild(const BedSettings& bed, const BedHeightmap& heightmap) {
    std::vector<MeshVertex> vertices;
    std::vector<uint32_t> indices;

    if (heightmap.cols >= 2 && heightmap.rows >= 2 &&
        static_cast<size_t>(heightmap.cols) * heightmap.rows == heightmap.elevationsMm.size()) {
        float maxAbs = 0.001f; // avoid dividing by zero when every sample is still 0 (nothing entered yet)
        for (float z : heightmap.elevationsMm) maxAbs = std::max(maxAbs, std::abs(z));

        float halfWidth = bed.widthMm * 0.5f;
        float halfDepth = bed.depthMm * 0.5f;
        float spacingX = bed.widthMm / static_cast<float>(heightmap.cols - 1);
        float spacingY = bed.depthMm / static_cast<float>(heightmap.rows - 1);

        vertices.reserve(static_cast<size_t>(heightmap.cols) * heightmap.rows);
        for (int row = 0; row < heightmap.rows; ++row) {
            for (int col = 0; col < heightmap.cols; ++col) {
                float elevation = heightmap.at(col, row);
                glm::vec3 position(bed.originXMm - halfWidth + col * spacingX,
                                    bed.originYMm - halfDepth + row * spacingY,
                                    bed.originZMm + elevation);
                float t = 0.5f + 0.5f * (elevation / maxAbs); // maps [-maxAbs, +maxAbs] -> [0, 1]
                glm::vec3 color = heatColor(t);
                vertices.push_back({position, glm::vec3(0.0f, 0.0f, 1.0f), color, 0.0f});
            }
        }

        // Two triangles per cell, sharing the grid's vertices (no
        // duplication) -- normals stay a flat "up" approximation rather
        // than being derived from neighboring cell slopes, which is fine
        // for a heatmap where COLOR carries the actual elevation signal,
        // not shading.
        for (int row = 0; row + 1 < heightmap.rows; ++row) {
            for (int col = 0; col + 1 < heightmap.cols; ++col) {
                uint32_t topLeft = static_cast<uint32_t>(row * heightmap.cols + col);
                uint32_t topRight = topLeft + 1;
                uint32_t bottomLeft = static_cast<uint32_t>((row + 1) * heightmap.cols + col);
                uint32_t bottomRight = bottomLeft + 1;
                indices.insert(indices.end(), {topLeft, bottomLeft, bottomRight, topLeft, bottomRight, topRight});
            }
        }
    }

    indexCount_ = static_cast<GLsizei>(indices.size());
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    GLsizei vertexCount = static_cast<GLsizei>(vertices.size());
    if (vertexCount > vboCapacityVertices_) {
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(MeshVertex)), vertices.data(), GL_DYNAMIC_DRAW);
        vboCapacityVertices_ = vertexCount;
    } else if (vertexCount > 0) {
        glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(vertices.size() * sizeof(MeshVertex)), vertices.data());
    }
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    if (indexCount_ > eboCapacityIndices_) {
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size() * sizeof(uint32_t)), indices.data(), GL_DYNAMIC_DRAW);
        eboCapacityIndices_ = indexCount_;
    } else if (indexCount_ > 0) {
        glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(indices.size() * sizeof(uint32_t)), indices.data());
    }
    glBindVertexArray(0);
}

void BedHeightmapRenderer::draw(const glm::mat4& viewProj, const LightingSettings& lighting) const {
    if (indexCount_ == 0) return;

    glm::vec3 dirs[LightingSettings::kMaxLights];
    glm::vec3 colors[LightingSettings::kMaxLights];
    int count = 0;
    for (const auto& light : lighting.lights) {
        if (!light.enabled || count >= LightingSettings::kMaxLights) continue;
        dirs[count] = light.direction;
        colors[count] = light.color;
        ++count;
    }

    glUseProgram(shaderProgram_);
    glUniformMatrix4fv(mvpLoc_, 1, GL_FALSE, glm::value_ptr(viewProj));
    glUniform3fv(lightDirsLoc_, count > 0 ? count : 1, glm::value_ptr(dirs[0]));
    glUniform3fv(lightColorsLoc_, count > 0 ? count : 1, glm::value_ptr(colors[0]));
    glUniform1i(lightCountLoc_, count);
    glUniform1i(selectionStyleLoc_, 0); // SelectionStyle::Outline == 0 -- no per-vertex tint effect, just plain lit color

    glDisable(GL_CULL_FACE); // visible from either side -- an operator orbiting under the bed shouldn't see it vanish
    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}
