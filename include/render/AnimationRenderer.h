#pragma once

#include "editor/PrintAnimation.h"
#include "render/LightingSettings.h"

#include <GL/glew.h>
#include <glm/glm.hpp>

// Draws the progressively-revealed print simulation: every
// AnimationSegment becomes a small, simple 4-sided "open tube" box (no
// mitred joints between neighboring segments -- unlike GeometryRenderer's
// always-visible bead mesh, a small seam here is an acceptable tradeoff
// for a preview, and building N independent boxes is far simpler than
// re-deriving mitred-run logic for possibly hundreds of thousands of
// small sub-segments). Each vertex carries the distance at which its
// segment starts printing; build() uploads the WHOLE mesh once (when
// playback starts, or the sequence/bead size changes), and draw() reveals
// it by moving a single uRevealDistanceMm uniform -- no per-frame CPU
// rebuild, so scrubbing stays smooth on a real 30k+ path file.
class AnimationRenderer {
public:
    AnimationRenderer();
    ~AnimationRenderer();

    AnimationRenderer(const AnimationRenderer&) = delete;
    AnimationRenderer& operator=(const AnimationRenderer&) = delete;

    void build(const AnimationSequence& sequence, float beadWidthMm, float beadHeightMm,
               const glm::vec3& printColor, const glm::vec3& travelColor);

    void draw(const glm::mat4& viewProj, const LightingSettings& lighting, float revealDistanceMm) const;

    size_t triangleCount() const { return static_cast<size_t>(indexCount_) / 3; }

private:
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint ebo_ = 0;
    GLuint shaderProgram_ = 0;
    GLint mvpLoc_ = -1;
    GLint lightDirsLoc_ = -1;
    GLint lightColorsLoc_ = -1;
    GLint lightCountLoc_ = -1;
    GLint revealLoc_ = -1;
    GLsizei indexCount_ = 0;
};
