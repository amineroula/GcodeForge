#pragma once

#include "render/LightingSettings.h"
#include "render/MeshShader.h"

#include <GL/glew.h>
#include <glm/glm.hpp>

// The moving print-head/nozzle marker during animation playback: a box
// (the head body) sitting above a thinner box (the nozzle), whose tip
// sits exactly at the current playback head position. Deliberately
// axis-aligned rather than oriented to the robot's real tool
// orientation (Path::a/b/c) -- the nozzle always points straight down at
// the bed regardless of travel direction, which is the visually
// meaningful part; matching the exact A/B/C tilt is a follow-up, not
// required for "does this look like it's printing."
//
// Rebuilds its (tiny, ~72-vertex) mesh every frame -- unlike
// AnimationRenderer's reveal mesh, this is cheap regardless of file
// size, since it's always just two boxes.
class PrintHeadRenderer {
public:
    PrintHeadRenderer();
    ~PrintHeadRenderer();

    PrintHeadRenderer(const PrintHeadRenderer&) = delete;
    PrintHeadRenderer& operator=(const PrintHeadRenderer&) = delete;

    // `nozzleTip` is where the nozzle touches down (the current playback
    // head position); the head body sits directly above it.
    void rebuild(const glm::vec3& nozzleTip, float headWidthMm, float headDepthMm, float headHeightMm,
                 float nozzleLengthMm, float nozzleWidthMm);
    void draw(const glm::mat4& viewProj, const LightingSettings& lighting) const;

private:
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint shaderProgram_ = 0;
    GLint mvpLoc_ = -1;
    GLint lightDirsLoc_ = -1;
    GLint lightColorsLoc_ = -1;
    GLint lightCountLoc_ = -1;
    GLint selectionStyleLoc_ = -1;
    GLint timeLoc_ = -1;
    GLint hasSelectionLoc_ = -1;
    GLsizei vertexCount_ = 0;
};
