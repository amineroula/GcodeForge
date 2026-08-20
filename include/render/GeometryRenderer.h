#pragma once

#include "model/Scene.h"
#include "render/LightingSettings.h"
#include "render/LineShader.h"
#include "render/MeshShader.h"
#include "render/PathColorizer.h"
#include "render/RenderSettings.h"

#include <GL/glew.h>
#include <glm/glm.hpp>

// The "geometry" view mode: print paths become solid rectangular "bead"
// boxes approximating the actual deposited material cross-section, instead
// of infinitely-thin lines. Travel paths still draw as thin lines (reusing
// the shared line shader) so nozzle-up moves stay visible -- there's
// nothing to show as a bead there, the nozzle isn't depositing anything.
//
// Optimization + correctness note (this exists because real SRC files can
// have 100k+ segments -- see docs/PLAN.md milestone 11): consecutive,
// position-connected print paths are merged into one continuous "run" and
// meshed as a single mitered tube -- N segments share N+1 cross-sections
// (4 vertices each) via an indexed triangle list, instead of each segment
// getting its own disconnected 8-vertex box with its own end caps. This is
// both a real triangle-count reduction (a run of N boxes needs exactly 2
// end caps total, not 2N) AND the fix for the gap/overlap artifact at
// corners: each cross-section's orientation is the MITER (average) of the
// incoming and outgoing segment directions, so adjacent segments share an
// exactly-matching cross-section instead of two independently-oriented
// ones. Per-vertex normals come from the cross-section offset only (not
// the along-length direction), which is why the shading reads as a smooth
// rounded bead rather than a faceted rectangular block.
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
    void draw(const glm::mat4& viewProj, const LightingSettings& lighting, bool backfaceCulling,
              SelectionStyle selectionStyle, float timeSeconds) const;

    size_t triangleCount() const { return static_cast<size_t>(indexCount_) / 3; }
    size_t outlineTriangleCount() const { return static_cast<size_t>(outlineIndexCount_) / 3; }

private:
    // Bead mesh (print paths): indexed triangle list, position+normal+color.
    GLuint meshVao_ = 0;
    GLuint meshVbo_ = 0;
    GLuint meshEbo_ = 0;
    GLuint meshShaderProgram_ = 0;
    GLint meshMvpLoc_ = -1;
    GLint meshLightDirsLoc_ = -1;
    GLint meshLightColorsLoc_ = -1;
    GLint meshLightCountLoc_ = -1;
    GLint meshSelectionStyleLoc_ = -1;
    GLint meshTimeLoc_ = -1;
    GLint meshHasSelectionLoc_ = -1;
    GLsizei indexCount_ = 0;
    GLsizei vboCapacityVertices_ = 0;
    GLsizei eboCapacityIndices_ = 0;
    bool hasSelection_ = false; // true when rebuild() found at least one selected print path -- gates the Pulse style's dimming

    // Travel-only lines: reuses the shared line shader/vertex layout.
    GLuint travelVao_ = 0;
    GLuint travelVbo_ = 0;
    GLuint travelShaderProgram_ = 0;
    GLint travelMvpLoc_ = -1;
    GLsizei travelVertexCount_ = 0;
    GLsizei travelVboCapacityVertices_ = 0;

    // Selection outline: a SEPARATE, slightly enlarged copy of just the
    // selected paths' bead mesh, drawn with front-face culling (only its
    // back faces render) before the normal mesh. This is the "inverted
    // hull" outline technique: the normal mesh's own front surface
    // (always closer to camera than the enlarged shell's far side) occludes
    // the shell's center, leaving only a rim visible right at the
    // silhouette edge -- angle-independent, unlike a fixed-pixel-width
    // line drawn along the centerline (which only reads as an "outline"
    // from some viewing angles and not others -- the actual reported bug
    // this replaces). Reuses appendRun/appendQuad, so winding correctness
    // (backface culling depends on it) carries over automatically.
    GLuint outlineVao_ = 0;
    GLuint outlineVbo_ = 0;
    GLuint outlineEbo_ = 0;
    GLsizei outlineIndexCount_ = 0;
    GLsizei outlineVboCapacityVertices_ = 0;
    GLsizei outlineEboCapacityIndices_ = 0;

    SpeedColorTable speedColors_;
};
