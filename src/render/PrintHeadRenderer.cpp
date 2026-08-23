#include "render/PrintHeadRenderer.h"

#include <glm/gtc/type_ptr.hpp>

namespace {

// Six faces, 2 triangles each, correct per-face normals -- a small,
// simple box builder. Appends 36 vertices to `out`.
void appendBox(std::vector<MeshVertex>& out, const glm::vec3& minCorner, const glm::vec3& maxCorner, const glm::vec3& color) {
    struct Face { glm::vec3 normal; glm::vec3 v0, v1, v2, v3; };
    glm::vec3 c000(minCorner.x, minCorner.y, minCorner.z), c100(maxCorner.x, minCorner.y, minCorner.z);
    glm::vec3 c010(minCorner.x, maxCorner.y, minCorner.z), c110(maxCorner.x, maxCorner.y, minCorner.z);
    glm::vec3 c001(minCorner.x, minCorner.y, maxCorner.z), c101(maxCorner.x, minCorner.y, maxCorner.z);
    glm::vec3 c011(minCorner.x, maxCorner.y, maxCorner.z), c111(maxCorner.x, maxCorner.y, maxCorner.z);

    Face faces[6] = {
        {{0, 0, -1}, c000, c010, c110, c100}, // bottom
        {{0, 0, 1}, c001, c101, c111, c011},  // top
        {{-1, 0, 0}, c000, c001, c011, c010}, // -X
        {{1, 0, 0}, c100, c110, c111, c101},  // +X
        {{0, -1, 0}, c000, c100, c101, c001}, // -Y
        {{0, 1, 0}, c010, c011, c111, c110},  // +Y
    };
    for (const auto& f : faces) {
        out.push_back({f.v0, f.normal, color, 0.0f});
        out.push_back({f.v1, f.normal, color, 0.0f});
        out.push_back({f.v2, f.normal, color, 0.0f});
        out.push_back({f.v0, f.normal, color, 0.0f});
        out.push_back({f.v2, f.normal, color, 0.0f});
        out.push_back({f.v3, f.normal, color, 0.0f});
    }
}

} // namespace

PrintHeadRenderer::PrintHeadRenderer() {
    shaderProgram_ = createMeshShaderProgram();
    mvpLoc_ = glGetUniformLocation(shaderProgram_, "uMvp");
    lightDirsLoc_ = glGetUniformLocation(shaderProgram_, "uLightDirs");
    lightColorsLoc_ = glGetUniformLocation(shaderProgram_, "uLightColors");
    lightCountLoc_ = glGetUniformLocation(shaderProgram_, "uLightCount");
    selectionStyleLoc_ = glGetUniformLocation(shaderProgram_, "uSelectionStyle");
    timeLoc_ = glGetUniformLocation(shaderProgram_, "uTime");
    hasSelectionLoc_ = glGetUniformLocation(shaderProgram_, "uHasSelection");

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), reinterpret_cast<void*>(offsetof(MeshVertex, position)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), reinterpret_cast<void*>(offsetof(MeshVertex, normal)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), reinterpret_cast<void*>(offsetof(MeshVertex, color)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), reinterpret_cast<void*>(offsetof(MeshVertex, selected)));
    glEnableVertexAttribArray(3);
    glBindVertexArray(0);
}

PrintHeadRenderer::~PrintHeadRenderer() {
    glDeleteVertexArrays(1, &vao_);
    glDeleteBuffers(1, &vbo_);
    glDeleteProgram(shaderProgram_);
}

void PrintHeadRenderer::rebuild(const glm::vec3& nozzleTip, float headWidthMm, float headDepthMm, float headHeightMm,
                                 float nozzleLengthMm, float nozzleWidthMm) {
    std::vector<MeshVertex> vertices;
    vertices.reserve(72);

    const glm::vec3 headColor(0.75f, 0.78f, 0.82f);
    const glm::vec3 nozzleColor(0.90f, 0.55f, 0.15f);

    // Nozzle: a thin box whose BOTTOM face touches the current print
    // point exactly, extending straight up.
    glm::vec3 nozzleMin(nozzleTip.x - nozzleWidthMm * 0.5f, nozzleTip.y - nozzleWidthMm * 0.5f, nozzleTip.z);
    glm::vec3 nozzleMax(nozzleTip.x + nozzleWidthMm * 0.5f, nozzleTip.y + nozzleWidthMm * 0.5f, nozzleTip.z + nozzleLengthMm);
    appendBox(vertices, nozzleMin, nozzleMax, nozzleColor);

    // Head body: sits directly on top of the nozzle.
    glm::vec3 headMin(nozzleTip.x - headWidthMm * 0.5f, nozzleTip.y - headDepthMm * 0.5f, nozzleMax.z);
    glm::vec3 headMax(nozzleTip.x + headWidthMm * 0.5f, nozzleTip.y + headDepthMm * 0.5f, nozzleMax.z + headHeightMm);
    appendBox(vertices, headMin, headMax, headColor);

    vertexCount_ = static_cast<GLsizei>(vertices.size());
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(MeshVertex)), vertices.data(), GL_DYNAMIC_DRAW);
    glBindVertexArray(0);
}

void PrintHeadRenderer::draw(const glm::mat4& viewProj, const LightingSettings& lighting) const {
    if (vertexCount_ == 0) return;

    glUseProgram(shaderProgram_);
    glUniformMatrix4fv(mvpLoc_, 1, GL_FALSE, glm::value_ptr(viewProj));
    glUniform1i(selectionStyleLoc_, 0); // Outline style == no special per-vertex effect, just lit shading
    glUniform1f(timeLoc_, 0.0f);
    glUniform1i(hasSelectionLoc_, 0);

    glm::vec3 dirs[LightingSettings::kMaxLights];
    glm::vec3 colors[LightingSettings::kMaxLights];
    int count = 0;
    for (const auto& light : lighting.lights) {
        if (!light.enabled || count >= LightingSettings::kMaxLights) continue;
        dirs[count] = -glm::normalize(light.direction);
        colors[count] = light.color;
        ++count;
    }
    glUniform3fv(lightDirsLoc_, count, glm::value_ptr(dirs[0]));
    glUniform3fv(lightColorsLoc_, count, glm::value_ptr(colors[0]));
    glUniform1i(lightCountLoc_, count);

    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, vertexCount_);
    glBindVertexArray(0);
}
