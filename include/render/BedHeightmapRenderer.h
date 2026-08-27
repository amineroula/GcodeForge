#pragma once

#include "model/BedHeightmap.h"
#include "render/BedSettings.h"
#include "render/LightingSettings.h"
#include "render/LineShader.h"
#include "render/MeshShader.h"

#include <GL/glew.h>
#include <glm/glm.hpp>

// Draws BedHeightmap as a colored surface over the bed: one quad (two
// triangles) per grid cell, Z offset by the measured elevation at each
// corner, colored by a blue (low) -> green (mid) -> red (high) heatmap
// ramp normalized to the largest elevation magnitude currently in the
// data. Reuses GeometryRenderer's MeshVertex layout/shader (position +
// normal + color + a "selected" float, always 0 here -- irrelevant unless
// someone points a Pulse/Stripes-style GeometryRenderer::draw() at this
// VAO, which nothing does) rather than writing a second near-identical
// shader.
class BedHeightmapRenderer {
public:
    BedHeightmapRenderer();
    ~BedHeightmapRenderer();

    BedHeightmapRenderer(const BedHeightmapRenderer&) = delete;
    BedHeightmapRenderer& operator=(const BedHeightmapRenderer&) = delete;

    // highlightCol/Row (-1 = none) override that ONE vertex's color to a
    // bright marker instead of its heatmap color -- live "which vertex
    // would a click affect right now" feedback for the paint tool (see
    // main.cpp's per-frame hover tracking while paint mode is active).
    void rebuild(const BedSettings& bed, const BedHeightmap& heightmap, int highlightCol = -1, int highlightRow = -1);
    void draw(const glm::mat4& viewProj, const LightingSettings& lighting) const;

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
    GLint selectionStyleLoc_ = -1;
    GLsizei indexCount_ = 0;
    GLsizei vboCapacityVertices_ = 0;
    GLsizei eboCapacityIndices_ = 0;

    // The heightmap's OWN cell boundaries, as lines. Without these the
    // surface is a flat unbroken sheet, and when every elevation is still
    // 0 it sits exactly coplanar with the bed reference grid -- so the
    // bed's 100mm lines read as though they were the heightmap's own
    // divisions. That's a real reported confusion: a 5x5 heightmap on a
    // 1000mm bed appeared to have 11 divisions, because those 11 lines
    // were the bed grid showing through. Drawing the actual cell edges
    // makes the real resolution unambiguous.
    GLuint edgeVao_ = 0;
    GLuint edgeVbo_ = 0;
    GLuint edgeShaderProgram_ = 0;
    GLint edgeMvpLoc_ = -1;
    GLsizei edgeVertexCount_ = 0;
    GLsizei edgeVboCapacityVertices_ = 0;
};
