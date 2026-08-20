#include "render/GeometryRenderer.h"

#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <cstdint>

namespace {

const glm::vec3 kWorldUp(0.0f, 0.0f, 1.0f);

// One cross-section's "right" unit vector (perpendicular to travel
// direction, in the horizontal plane). At an interior joint between two
// segments, this uses the AVERAGE of the incoming and outgoing directions
// (a simple miter) rather than either segment's own direction alone --
// that's what makes adjacent boxes share a matching cross-section instead
// of leaving a gap/overlap at the corner, which was the reported artifact.
glm::vec3 crossSectionRight(const glm::vec3& incomingDir, const glm::vec3& outgoingDir) {
    glm::vec3 dir = incomingDir + outgoingDir;
    float len = glm::length(dir);
    glm::vec3 dirUnit = (len > 1e-4f) ? dir / len : incomingDir; // near-180-degree reversal: fall back to just one side
    glm::vec3 right = glm::cross(dirUnit, kWorldUp);
    if (glm::length(right) < 1e-4f) return glm::vec3(1.0f, 0.0f, 0.0f); // segment is vertical
    return glm::normalize(right);
}

// Appends two triangles for a planar quad (v0,v1,v2,v3 in order around the
// perimeter), choosing winding order so the resulting face normal points
// toward `expectedOutward` -- rather than hardcoding an index order and
// hoping it's outward-facing (an earlier version of this function did
// exactly that and got it wrong for 5 of the box's 6 faces; verified by
// hand-deriving the actual winding it produced). This makes correctness
// self-verifying at build time instead of resting on manual derivation.
void appendQuad(std::vector<MeshVertex>& vertices, std::vector<uint32_t>& indices,
                 uint32_t v0, uint32_t v1, uint32_t v2, uint32_t v3, const glm::vec3& expectedOutward) {
    glm::vec3 edge1 = vertices[v1].position - vertices[v0].position;
    glm::vec3 edge2 = vertices[v2].position - vertices[v0].position;
    glm::vec3 faceNormal = glm::cross(edge1, edge2);

    if (glm::dot(faceNormal, expectedOutward) >= 0.0f) {
        indices.insert(indices.end(), {v0, v1, v2, v0, v2, v3});
    } else {
        indices.insert(indices.end(), {v0, v2, v1, v0, v3, v2});
    }
}

// Appends one continuous "bead" run as a single mitered tube: N segments
// share N+1 cross-sections (4 vertices each, 8 total for the whole run's
// two ends plus interior joints), rather than each segment getting its own
// disconnected 8-vertex box. This is both the gap fix and a real triangle
// reduction: N boxes independently would need 2N end caps; a run needs
// exactly 2, one at each true end.
void appendRun(std::vector<MeshVertex>& vertices, std::vector<uint32_t>& indices,
               const std::vector<glm::vec3>& runPoints, const std::vector<glm::vec3>& runColors,
               float halfWidth, float halfHeight) {
    size_t segmentCount = runColors.size();
    if (segmentCount == 0) return;

    std::vector<glm::vec3> rightUnits(segmentCount + 1);
    for (size_t i = 0; i <= segmentCount; ++i) {
        glm::vec3 incoming = (i == 0) ? (runPoints[1] - runPoints[0]) : (runPoints[i] - runPoints[i - 1]);
        glm::vec3 outgoing = (i == segmentCount) ? (runPoints[segmentCount] - runPoints[segmentCount - 1])
                                                  : (runPoints[i + 1] - runPoints[i]);
        float inLen = glm::length(incoming), outLen = glm::length(outgoing);
        glm::vec3 inUnit = (inLen > 1e-6f) ? incoming / inLen : glm::vec3(1.0f, 0.0f, 0.0f);
        glm::vec3 outUnit = (outLen > 1e-6f) ? outgoing / outLen : glm::vec3(1.0f, 0.0f, 0.0f);
        rightUnits[i] = crossSectionRight(inUnit, outUnit);
    }

    uint32_t base = static_cast<uint32_t>(vertices.size());
    for (size_t i = 0; i <= segmentCount; ++i) {
        glm::vec3 right = rightUnits[i] * halfWidth;
        glm::vec3 up = kWorldUp * halfHeight;
        glm::vec3 p = runPoints[i];
        // The very last cross-section has no "owning" segment (it's the
        // end of the last one) -- reuse that segment's color.
        glm::vec3 color = runColors[i < segmentCount ? i : segmentCount - 1];

        glm::vec3 corners[4] = {p - right - up, p + right - up, p + right + up, p - right + up};
        glm::vec3 normals[4] = {
            glm::normalize(-rightUnits[i] - kWorldUp),
            glm::normalize(rightUnits[i] - kWorldUp),
            glm::normalize(rightUnits[i] + kWorldUp),
            glm::normalize(-rightUnits[i] + kWorldUp),
        };
        for (int c = 0; c < 4; ++c) vertices.push_back({corners[c], normals[c], color});
    }

    for (size_t i = 0; i < segmentCount; ++i) {
        uint32_t a = base + static_cast<uint32_t>(i * 4);
        uint32_t b = base + static_cast<uint32_t>((i + 1) * 4);
        glm::vec3 approxRight = (rightUnits[i] + rightUnits[i + 1]);
        if (glm::length(approxRight) > 1e-6f) approxRight = glm::normalize(approxRight);
        // Corner order per cross-section: 0=(-right,-up) 1=(+right,-up) 2=(+right,+up) 3=(-right,+up)
        appendQuad(vertices, indices, a + 0, a + 1, b + 1, b + 0, -kWorldUp);    // bottom, outward = -up
        appendQuad(vertices, indices, a + 3, b + 3, b + 2, a + 2, kWorldUp);     // top, outward = +up
        appendQuad(vertices, indices, a + 1, a + 2, b + 2, b + 1, approxRight);  // +right side, outward = +right
        appendQuad(vertices, indices, a + 0, b + 0, b + 3, a + 3, -approxRight); // -right side, outward = -right
    }

    glm::vec3 startDir = glm::normalize(runPoints[1] - runPoints[0]);
    appendQuad(vertices, indices, base + 0, base + 1, base + 2, base + 3, -startDir); // start cap, outward = backward

    uint32_t last = base + static_cast<uint32_t>(segmentCount * 4);
    glm::vec3 endDir = glm::normalize(runPoints[segmentCount] - runPoints[segmentCount - 1]);
    appendQuad(vertices, indices, last + 0, last + 1, last + 2, last + 3, endDir); // end cap, outward = forward
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
        const auto& paths = object.paths;

        size_t i = 0;
        while (i < paths.size()) {
            if (paths[i].type != PathType::Print) {
                glm::vec3 fromWorld(applyTransform(object.transform, paths[i].from));
                glm::vec3 toWorld(applyTransform(object.transform, paths[i].to));
                bool selected = object.selectedPaths.count(paths[i].number) > 0;
                glm::vec3 color = selected ? selectionHighlightColor() : pathColor(object, paths[i], colorMode, speedColors_);
                travelVertices.push_back({fromWorld, color});
                travelVertices.push_back({toWorld, color});
                ++i;
                continue;
            }

            // Extend the run while consecutive print paths are
            // position-connected (this path's `to` matches the next
            // path's `from`) -- a real gap in the source data (not just a
            // direction change) still correctly starts a new run/new caps.
            size_t runEnd = i;
            while (runEnd + 1 < paths.size()) {
                const Path& cur = paths[runEnd];
                const Path& next = paths[runEnd + 1];
                if (next.type != PathType::Print) break;
                if (glm::length(next.from - cur.to) > 1e-4) break;
                ++runEnd;
            }

            std::vector<glm::vec3> runPoints;
            std::vector<glm::vec3> runColors;
            runPoints.push_back(glm::vec3(applyTransform(object.transform, paths[i].from)));
            for (size_t k = i; k <= runEnd; ++k) {
                runPoints.push_back(glm::vec3(applyTransform(object.transform, paths[k].to)));
                bool selected = object.selectedPaths.count(paths[k].number) > 0;
                runColors.push_back(selected ? selectionHighlightColor() : pathColor(object, paths[k], colorMode, speedColors_));
            }
            appendRun(meshVertices, meshIndices, runPoints, runColors, halfWidth, halfHeight);

            i = runEnd + 1;
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

void GeometryRenderer::draw(const glm::mat4& viewProj, const glm::vec3& lightDir, bool backfaceCulling) const {
    if (backfaceCulling) {
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
    } else {
        glDisable(GL_CULL_FACE);
    }

    if (indexCount_ > 0) {
        glUseProgram(meshShaderProgram_);
        glUniformMatrix4fv(meshMvpLoc_, 1, GL_FALSE, glm::value_ptr(viewProj));
        glUniform3fv(meshLightDirLoc_, 1, glm::value_ptr(lightDir));
        glBindVertexArray(meshVao_);
        glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, nullptr);
        glBindVertexArray(0);
    }

    glDisable(GL_CULL_FACE); // don't leak culling state into travel-line/other draw calls below

    if (travelVertexCount_ > 0) {
        glUseProgram(travelShaderProgram_);
        glUniformMatrix4fv(travelMvpLoc_, 1, GL_FALSE, glm::value_ptr(viewProj));
        glBindVertexArray(travelVao_);
        glDrawArrays(GL_LINES, 0, travelVertexCount_);
        glBindVertexArray(0);
    }
}
