#pragma once

#include "model/Scene.h"
#include "render/LineShader.h"
#include "render/MeshShader.h"
#include "render/PathColorizer.h"

#include <GL/glew.h>
#include <glm/glm.hpp>

// The "geometry" view mode: print paths become solid rectangular "bead"
// boxes approximating the actual deposited material cross-section, instead
// of infinitely-thin lines. Travel paths still draw as thin lines (reusing
// the shared line shader) so nozzle-up moves stay visible -- there's
// nothing to show as a bead there, the nozzle isn't depositing anything.
//
// Optimization note (this exists because real SRC files can have
// 100k+ segments -- see docs/PLAN.md milestone 11): each box uses only 8
// vertices with an indexed triangle list (36 indices for 12 triangles),
// not 36 raw vertices, so vertex memory doesn't triple for no reason.
// Per-vertex normals are computed from the cross-section offset only (not
// the along-length direction), which both avoids a lighting artifact on
// long thin segments AND means the two ends of a box share vertex normals
// with adjacent boxes' matching corners -- not exploited yet (still one
// draw call worth of disjoint boxes), but it's why the shading looks like
// a smooth rounded bead rather than a faceted rectangular block.
//
// This is deliberately NOT the full adaptive-LOD system (culling,
// screen-space simplification) -- that's milestone 11's job. What IS done
// here, consistent with SceneRenderer: rebuild() only runs when the caller
// decides something changed, never per-frame.
class GeometryRenderer {
public:
    GeometryRenderer();
    ~GeometryRenderer();

    GeometryRenderer(const GeometryRenderer&) = delete;
    GeometryRenderer& operator=(const GeometryRenderer&) = delete;

    void rebuild(const Scene& scene, ColorMode colorMode, float beadWidthMm, float beadHeightMm);
    void draw(const glm::mat4& viewProj, const glm::vec3& lightDir) const;

    size_t triangleCount() const { return static_cast<size_t>(indexCount_) / 3; }

private:
    // Bead mesh (print paths): indexed triangle list, position+normal+color.
    GLuint meshVao_ = 0;
    GLuint meshVbo_ = 0;
    GLuint meshEbo_ = 0;
    GLuint meshShaderProgram_ = 0;
    GLint meshMvpLoc_ = -1;
    GLint meshLightDirLoc_ = -1;
    GLsizei indexCount_ = 0;
    GLsizei vboCapacityVertices_ = 0;
    GLsizei eboCapacityIndices_ = 0;

    // Travel-only lines: reuses the shared line shader/vertex layout.
    GLuint travelVao_ = 0;
    GLuint travelVbo_ = 0;
    GLuint travelShaderProgram_ = 0;
    GLint travelMvpLoc_ = -1;
    GLsizei travelVertexCount_ = 0;
    GLsizei travelVboCapacityVertices_ = 0;

    SpeedColorTable speedColors_;
};
