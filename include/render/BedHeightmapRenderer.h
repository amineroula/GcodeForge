#pragma once

#include "model/BedHeightmap.h"
#include "render/BedSettings.h"
#include "render/LightingSettings.h"
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

    void rebuild(const BedSettings& bed, const BedHeightmap& heightmap);
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
};
