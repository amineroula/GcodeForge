#include "render/AnimationRenderer.h"
#include "render/AnimationShader.h"

#include <glm/gtc/type_ptr.hpp>

namespace {

void uploadLights(GLint dirsLoc, GLint colorsLoc, GLint countLoc, const LightingSettings& lighting) {
    glm::vec3 dirs[LightingSettings::kMaxLights];
    glm::vec3 colors[LightingSettings::kMaxLights];
    int count = 0;
    for (const auto& light : lighting.lights) {
        if (!light.enabled || count >= LightingSettings::kMaxLights) continue;
        dirs[count] = -glm::normalize(light.direction); // same "toward the light" convention MeshShader uses
        colors[count] = light.color;
        ++count;
    }
    glUniform3fv(dirsLoc, count, glm::value_ptr(dirs[0]));
    glUniform3fv(colorsLoc, count, glm::value_ptr(colors[0]));
    glUniform1i(countLoc, count);
}

} // namespace

AnimationRenderer::AnimationRenderer() {
    shaderProgram_ = createAnimationShaderProgram();
    mvpLoc_ = glGetUniformLocation(shaderProgram_, "uMvp");
    lightDirsLoc_ = glGetUniformLocation(shaderProgram_, "uLightDirs");
    lightColorsLoc_ = glGetUniformLocation(shaderProgram_, "uLightColors");
    lightCountLoc_ = glGetUniformLocation(shaderProgram_, "uLightCount");
    revealLoc_ = glGetUniformLocation(shaderProgram_, "uRevealDistanceMm");

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ebo_);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(AnimationVertex), reinterpret_cast<void*>(offsetof(AnimationVertex, position)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(AnimationVertex), reinterpret_cast<void*>(offsetof(AnimationVertex, normal)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(AnimationVertex), reinterpret_cast<void*>(offsetof(AnimationVertex, color)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(AnimationVertex), reinterpret_cast<void*>(offsetof(AnimationVertex, revealAtMm)));
    glEnableVertexAttribArray(3);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBindVertexArray(0);
}

AnimationRenderer::~AnimationRenderer() {
    glDeleteVertexArrays(1, &vao_);
    glDeleteBuffers(1, &vbo_);
    glDeleteBuffers(1, &ebo_);
    glDeleteProgram(shaderProgram_);
}

void AnimationRenderer::build(const AnimationSequence& sequence, float beadWidthMm, float beadHeightMm,
                               const glm::vec3& printColor, const glm::vec3& travelColor) {
    std::vector<AnimationVertex> vertices;
    std::vector<GLuint> indices;
    vertices.reserve(sequence.segments.size() * 8);
    indices.reserve(sequence.segments.size() * 24);

    const glm::vec3 worldUp(0.0f, 0.0f, 1.0f);

    for (const auto& seg : sequence.segments) {
        glm::vec3 from(seg.from);
        glm::vec3 to(seg.to);
        glm::vec3 dir = to - from;
        float len = glm::length(dir);
        if (len < 1e-6f) continue;
        glm::vec3 forward = dir / len;

        glm::vec3 right = glm::cross(forward, worldUp);
        right = (glm::length(right) > 1e-6f) ? glm::normalize(right) : glm::vec3(1.0f, 0.0f, 0.0f);
        glm::vec3 up = glm::normalize(glm::cross(right, forward));

        bool isPrint = (seg.type == PathType::Print);
        float halfW = (isPrint ? beadWidthMm : beadWidthMm * 0.15f) * 0.5f;
        float halfH = (isPrint ? beadHeightMm : beadHeightMm * 0.15f) * 0.5f;
        glm::vec3 color = isPrint ? printColor : travelColor;
        float revealAt = static_cast<float>(seg.startDistanceMm);

        glm::vec3 offsets[4] = {
            -right * halfW - up * halfH,
            right * halfW - up * halfH,
            right * halfW + up * halfH,
            -right * halfW + up * halfH,
        };

        GLuint base = static_cast<GLuint>(vertices.size());
        for (int i = 0; i < 4; ++i) {
            glm::vec3 normal = (glm::length(offsets[i]) > 1e-6f) ? glm::normalize(offsets[i]) : right;
            vertices.push_back({from + offsets[i], normal, color, revealAt});
        }
        for (int i = 0; i < 4; ++i) {
            glm::vec3 normal = (glm::length(offsets[i]) > 1e-6f) ? glm::normalize(offsets[i]) : right;
            vertices.push_back({to + offsets[i], normal, color, revealAt});
        }

        for (int i = 0; i < 4; ++i) {
            GLuint i0 = base + static_cast<GLuint>(i);
            GLuint i1 = base + static_cast<GLuint>((i + 1) % 4);
            GLuint j0 = i0 + 4;
            GLuint j1 = i1 + 4;
            indices.push_back(i0); indices.push_back(i1); indices.push_back(j1);
            indices.push_back(i0); indices.push_back(j1); indices.push_back(j0);
        }
    }

    indexCount_ = static_cast<GLsizei>(indices.size());

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(AnimationVertex)),
                 vertices.empty() ? nullptr : vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size() * sizeof(GLuint)),
                 indices.empty() ? nullptr : indices.data(), GL_STATIC_DRAW);
    glBindVertexArray(0);
}

void AnimationRenderer::draw(const glm::mat4& viewProj, const LightingSettings& lighting, float revealDistanceMm) const {
    if (indexCount_ == 0) return;

    glUseProgram(shaderProgram_);
    glUniformMatrix4fv(mvpLoc_, 1, GL_FALSE, glm::value_ptr(viewProj));
    glUniform1f(revealLoc_, revealDistanceMm);
    uploadLights(lightDirsLoc_, lightColorsLoc_, lightCountLoc_, lighting);

    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}
