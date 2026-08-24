// A small hand-checked regression test for SrcParser and GcodeParser.
// No test framework -- just asserts against numbers worked out by hand by
// tracing the parser's algorithm line by line (see the comments below).
// Run via `ctest` from the build directory, or the executable directly.

#include "parser/SrcParser.h"
#include "parser/GcodeParser.h"
#include "editor/Selection.h"
#include "editor/SpeedEditing.h"
#include "editor/UndoStack.h"
#include "editor/Picking.h"
#include "io/BedIO.h"
#include "io/ProjectIO.h"
#include "editor/Gizmo.h"
#include "editor/SrcExporter.h"
#include "editor/BedConform.h"
#include "render/PathColorizer.h"
#include "editor/ConnectedDrag.h"
#include "editor/Framing.h"
#include "editor/InterleavePrint.h"
#include "editor/MirrorObject.h"
#include "editor/ObjectLinking.h"
#include "editor/PathSplit.h"
#include "editor/RotatePaths.h"
#include "editor/CellTemplate.h"
#include "editor/ExportValidation.h"
#include "editor/PrintAnimation.h"
#include "parser/DxfParser.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <limits>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <vector>
#include <string>
#include <cmath>

namespace {

int g_failures = 0;

void check(bool condition, const char* description) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", description);
        ++g_failures;
    } else {
        std::printf("PASS: %s\n", description);
    }
}

void checkNear(double actual, double expected, const char* description) {
    check(std::abs(actual - expected) < 1e-6, description);
}

// A realistic small KUKA SRC snippet: an approach/travel section wrapped in
// the editor's own ";TRAVEL START"/";TRAVEL END" markers, then a two-layer
// print. Hand-traced expected result: 7 paths total (2 travel + 5 print),
// 2 layers (paths 3-5 at Z=82, paths 6-7 at Z=84).
void testSrcParser() {
    std::vector<std::string> lines = {
        "DEF Chair_01()",
        ";TRAVEL START",
        "$VEL.CP = 0.100",
        "PTP {X 1000,Y 500,Z 300}",
        "LIN {X 1000,Y 500,Z 82,A 90,B 0,C 180}",
        ";TRAVEL END",
        "$VEL.CP = 0.040",
        "LIN {X 1010,Y 500,Z 82}",
        "LIN {X 1020,Y 510,Z 82}",
        "LIN {X 1030,Y 520,Z 82}",
        "LIN {X 1030,Y 520,Z 84}",
        "LIN {X 1040,Y 530,Z 84}",
        "END",
    };

    SceneObject object = parseSrc("Chair_01", lines);

    check(object.paths.size() == 7, "SRC: parses 7 motion paths");
    check(object.layers.size() == 2, "SRC: detects 2 print layers");

    if (object.paths.size() == 7) {
        check(object.paths[0].type == PathType::Travel, "SRC: path 1 (PTP before marker end) is travel");
        check(object.paths[1].type == PathType::Travel, "SRC: path 2 (LIN before marker end) is travel");
        check(object.paths[2].type == PathType::Print, "SRC: path 3 (first LIN after travel end) is print");
        check(object.paths[2].layer == 1, "SRC: path 3 is on layer 1");
        check(object.paths[4].layer == 1, "SRC: path 5 is still on layer 1 (same Z)");
        check(object.paths[5].layer == 2, "SRC: path 6 starts layer 2 (Z changed)");
        checkNear(object.paths[2].to.z, 82.0, "SRC: path 3 target Z is 82");
        checkNear(object.paths[5].to.z, 84.0, "SRC: path 6 target Z is 84");
        check(object.paths[2].speed.has_value() && std::abs(*object.paths[2].speed - 0.040) < 1e-9,
              "SRC: path 3 picked up the $VEL.CP = 0.040 in effect at that line");
        check(object.paths[1].speed.has_value() && std::abs(*object.paths[1].speed - 0.100) < 1e-9,
              "SRC: path 2 picked up the earlier $VEL.CP = 0.100");
        check(object.paths[1].a.has_value() && object.paths[1].b.has_value() && object.paths[1].c.has_value(),
              "SRC: path 2's A/B/C orientation is captured, not silently dropped");
        if (object.paths[1].a && object.paths[1].b && object.paths[1].c) {
            checkNear(*object.paths[1].a, 90.0, "SRC: path 2 A = 90");
            checkNear(*object.paths[1].b, 0.0, "SRC: path 2 B = 0");
            checkNear(*object.paths[1].c, 180.0, "SRC: path 2 C = 180");
        }
        check(!object.paths[2].a.has_value(), "SRC: a path without A/B/C in its source line has no orientation (not a false 0)");
    }

    if (object.layers.size() == 2) {
        check(object.layers[0].startPath == 3 && object.layers[0].endPath == 5, "SRC: layer 1 spans paths 3-5");
        check(object.layers[1].startPath == 6 && object.layers[1].endPath == 7, "SRC: layer 2 spans paths 6-7");
    }
}

// G0 travel, then a 3-point print move that changes Z once -- expect 1
// travel path, 3 print paths across 2 layers.
void testGcodeParser() {
    std::vector<std::string> lines = {
        "G0 X0 Y0 Z10",
        "G1 X10 Y0 Z10 F0.05",
        "G1 X10 Y10 Z10",
        "G1 X10 Y10 Z12",
    };

    SceneObject object = parseGcode("test_part", lines);

    check(object.paths.size() == 4, "Gcode: parses 4 motion paths");
    if (object.paths.size() == 4) {
        check(object.paths[0].type == PathType::Travel, "Gcode: G0 is travel");
        check(object.paths[1].type == PathType::Print, "Gcode: G1 is print");
        check(object.paths[1].speed.has_value() && std::abs(*object.paths[1].speed - 0.05) < 1e-9,
              "Gcode: F sets speed");
        check(object.paths[3].layer == 2, "Gcode: Z change on path 4 starts a new layer");
    }
}

// Selection compose (replace/add/subtract) and speed application
// (exact/reduce/increase, PTP skip) against a small hand-built object --
// no file parsing involved, this exercises editor/ in isolation.
void testEditorLogic() {
    SceneObject object;
    object.paths.push_back(Path{1, {0,0,0}, {1,0,0}, PathType::Print, 1, "LIN", 0.040, std::nullopt, 0});
    object.paths.push_back(Path{2, {1,0,0}, {2,0,0}, PathType::Print, 1, "LIN", 0.040, std::nullopt, 1});
    object.paths.push_back(Path{3, {2,0,0}, {3,0,0}, PathType::Print, 2, "LIN", 0.040, std::nullopt, 2});
    object.paths.push_back(Path{4, {3,0,0}, {4,0,0}, PathType::Travel, -1, "PTP", 0.100, std::nullopt, 3});

    // Selection compose.
    applySelectionCompose(object.selectedPaths, {1, 2}, SelectionCompose::Replace);
    check(object.selectedPaths.size() == 2, "Selection: replace sets {1,2}");

    applySelectionCompose(object.selectedPaths, {3}, SelectionCompose::Add);
    check(object.selectedPaths.count(1) && object.selectedPaths.count(2) && object.selectedPaths.count(3),
          "Selection: add unions in {3}");

    applySelectionCompose(object.selectedPaths, {2}, SelectionCompose::Subtract);
    check(!object.selectedPaths.count(2) && object.selectedPaths.size() == 2,
          "Selection: subtract removes {2}, leaves {1,3}");

    // Layer-table click selects all print paths on that layer.
    std::vector<int> layer1Paths = pathNumbersForLayer(object, 1);
    check(layer1Paths.size() == 2 && layer1Paths[0] == 1 && layer1Paths[1] == 2,
          "Selection: layer 1 has paths {1,2}");

    // Speed: exact sets an absolute value.
    SpeedApplyResult exactResult = applySpeedToPaths(object, {1}, SpeedApplyMode::Exact, 0.025);
    check(exactResult.appliedCount == 1, "Speed: exact applies to 1 path");
    check(object.paths[0].speedOverride.has_value() && std::abs(*object.paths[0].speedOverride - 0.025) < 1e-9,
          "Speed: path 1 override is exactly 0.025");

    // Speed: reduce compounds on the CURRENT effective speed (the override
    // just set), not the originally parsed speed.
    applySpeedToPaths(object, {1}, SpeedApplyMode::Reduce, 20.0); // 0.025 * 0.8 = 0.020
    checkNear(*object.paths[0].speedOverride, 0.020, "Speed: reduce 20% compounds on the prior override (0.025 -> 0.020)");

    // Speed: PTP paths are skipped, not overridden.
    SpeedApplyResult ptpResult = applySpeedToPaths(object, {4}, SpeedApplyMode::Exact, 0.5);
    check(ptpResult.appliedCount == 0 && ptpResult.skippedPtpCount == 1, "Speed: PTP path is skipped, not overridden");
    check(!object.paths[3].speedOverride.has_value(), "Speed: PTP path has no override after being skipped");
}

// Whole-scene snapshot undo/redo, including the continuous-edit path
// (begin/commit) that a drag or text-field edit uses to avoid pushing one
// undo entry per frame while a value is being dragged.
void testUndoStack() {
    Scene scene;
    SceneObject object;
    object.name = "A";
    object.transform.x = 0.0;
    scene.addObject(object);

    UndoStack undo;
    undo.snapshotBeforeChange(scene);
    scene.objects[0].transform.x = 50.0;

    check(undo.canUndo(), "UndoStack: canUndo is true after a snapshot");
    undo.undo(scene);
    checkNear(scene.objects[0].transform.x, 0.0, "UndoStack: undo restores the pre-change X");
    check(undo.canRedo(), "UndoStack: canRedo is true after an undo");
    undo.redo(scene);
    checkNear(scene.objects[0].transform.x, 50.0, "UndoStack: redo re-applies the change");

    // Continuous edit: begin once at drag start, mutate across several
    // "frames," commit once. Undo should land back at the PRE-drag value,
    // not some intermediate value from partway through the drag.
    Scene dragScene;
    SceneObject dragObject;
    dragObject.name = "B";
    dragObject.transform.x = 1.0;
    dragScene.addObject(dragObject);

    UndoStack dragUndo;
    dragUndo.beginContinuousEdit(dragScene);
    dragScene.objects[0].transform.x = 2.0; // simulated drag frame 1
    dragUndo.beginContinuousEdit(dragScene); // should be a no-op (already capturing) -- must NOT overwrite the pre-drag snapshot with 2.0
    dragScene.objects[0].transform.x = 3.0; // simulated drag frame 2
    dragUndo.commitContinuousEdit();

    check(dragUndo.canUndo(), "UndoStack: committing a continuous edit pushes exactly one entry");
    dragUndo.undo(dragScene);
    checkNear(dragScene.objects[0].transform.x, 1.0, "UndoStack: continuous-edit undo restores the PRE-drag value, not an intermediate one");
}

// Screen-space picking: project known world points through a known
// orthographic projection (world XY maps predictably to screen pixels) so
// expected pixel positions can be hand-computed rather than guessed.
void testPicking() {
    // ortho(-100,100,-100,100): world x in [-100,100] -> screen x in [0,200];
    // world y in [-100,100] -> screen y in [200,0] (Y flips: NDC is up, screen is down).
    glm::mat4 projection = glm::ortho(-100.0f, 100.0f, -100.0f, 100.0f, -100.0f, 100.0f);
    ScreenProjector projector{projection, 200.0f, 200.0f};

    Scene crossScene;
    SceneObject crossObject;
    crossObject.name = "cross";
    Path horizontal; horizontal.number = 1; horizontal.type = PathType::Print;
    horizontal.from = glm::dvec3(-50, 0, 0); horizontal.to = glm::dvec3(50, 0, 0);
    Path vertical; vertical.number = 2; vertical.type = PathType::Print;
    vertical.from = glm::dvec3(0, -50, 0); vertical.to = glm::dvec3(0, 50, 0);
    crossObject.paths = {horizontal, vertical};
    crossScene.addObject(crossObject);

    // Screen (50,100): sits exactly on the horizontal line (screen y=100),
    // 50px away from the vertical line (screen x=100) -- nearest should be path 1.
    auto hit = pickNearestPath(crossScene, projector, glm::vec2(50.0f, 100.0f), 10.0f);
    check(hit.has_value() && hit->pathNumber == 1, "Picking: nearest-path finds the horizontal line, not the vertical one");

    auto miss = pickNearestPath(crossScene, projector, glm::vec2(0.0f, 0.0f), 5.0f);
    check(!miss.has_value(), "Picking: a point far from both lines (beyond pickRadius) finds nothing");

    Scene rectScene;
    SceneObject rectObject;
    rectObject.name = "rectTest";
    Path bottomLeft; bottomLeft.number = 1; bottomLeft.type = PathType::Print;
    bottomLeft.from = glm::dvec3(-80, -80, 0); bottomLeft.to = glm::dvec3(-20, -80, 0); // screen midpoint (50,180)
    Path topRight; topRight.number = 2; topRight.type = PathType::Print;
    topRight.from = glm::dvec3(20, 80, 0); topRight.to = glm::dvec3(80, 80, 0); // screen midpoint (150,20)
    rectObject.paths = {bottomLeft, topRight};
    rectScene.addObject(rectObject);

    auto rectHits = pickPathsInRect(rectScene, projector, glm::vec2(0.0f, 150.0f), glm::vec2(100.0f, 200.0f));
    check(rectHits.size() == 1 && rectHits[0].pathNumber == 1,
          "Picking: rectangle select finds only the path whose midpoint falls inside it");
}

// A path whose MIDPOINT is outside the marquee rectangle, but one end
// grazes a corner of it, must still be selected -- this was the reported
// "drag selection only selects when inside the rectangle" complaint;
// the fix moved from midpoint-containment to real segment intersection.
void testMarqueeTouch() {
    glm::mat4 projection = glm::ortho(-100.0f, 100.0f, -100.0f, 100.0f, -100.0f, 100.0f);
    ScreenProjector projector{projection, 200.0f, 200.0f};

    Scene scene;
    SceneObject object;
    object.name = "grazer";
    Path grazing; grazing.number = 1; grazing.type = PathType::Print;
    grazing.from = glm::dvec3(-80, 80, 0); // screen (20,20)
    grazing.to = glm::dvec3(-20, 20, 0);   // screen (80,80); midpoint screen (50,50)
    object.paths = {grazing};
    scene.addObject(object);

    // Rect only covers the corner near the path's START point (20,20);
    // the path's midpoint (50,50) is well outside this rect.
    auto hits = pickPathsInRect(scene, projector, glm::vec2(0.0f, 0.0f), glm::vec2(30.0f, 30.0f));
    check(hits.size() == 1 && hits[0].pathNumber == 1,
          "Picking: marquee selects a path that only grazes a corner of the rect, not just ones whose midpoint is inside");
}

// selectBackfacing controls whether picking prefers the camera-nearest
// candidate over the merely-closer-on-screen one. Uses the projector's
// own computed depth (rather than assuming a Z/NDC sign convention) to
// decide which path is actually nearer, so the test is convention-proof.
void testPickingBackfacing() {
    glm::mat4 projection = glm::ortho(-100.0f, 100.0f, -100.0f, 100.0f, -100.0f, 100.0f);
    ScreenProjector projector{projection, 200.0f, 200.0f};

    Scene scene;
    SceneObject object;
    object.name = "depthTest";
    Path pathA; pathA.number = 1; pathA.type = PathType::Print; // screen x=10 -- exactly on the click point
    pathA.from = glm::dvec3(-95, 0, 50); pathA.to = glm::dvec3(-85, 0, 50);
    Path pathB; pathB.number = 2; pathB.type = PathType::Print; // screen x=35 -- 25px from the click point
    pathB.from = glm::dvec3(-70, 0, -50); pathB.to = glm::dvec3(-60, 0, -50);
    object.paths = {pathA, pathB};
    scene.addObject(object);

    float depthA = projector.project(glm::vec3(-90.0f, 0.0f, 50.0f))->z;
    float depthB = projector.project(glm::vec3(-65.0f, 0.0f, -50.0f))->z;
    int nearerPathNumber = (depthA < depthB) ? 1 : 2;

    glm::vec2 clickPoint(10.0f, 100.0f); // exactly on path A; 25px from path B

    auto withBackfacing = pickNearestPath(scene, projector, clickPoint, 30.0f, /*selectBackfacing=*/true);
    check(withBackfacing.has_value() && withBackfacing->pathNumber == 1,
          "Picking: selectBackfacing=true picks the 2D-nearest path regardless of which is actually closer to the camera");

    auto withoutBackfacing = pickNearestPath(scene, projector, clickPoint, 30.0f, /*selectBackfacing=*/false);
    check(withoutBackfacing.has_value() && withoutBackfacing->pathNumber == nearerPathNumber,
          "Picking: selectBackfacing=false prefers the path nearer the camera, even when it's farther away on screen");
}

// Save then load a BedSettings + BedHeightmap and check every field
// round-trips exactly.
void testBedIO() {
    BedSettings original;
    original.widthMm = 1234.5f;
    original.depthMm = 678.0f;
    original.originXMm = -50.0f;
    original.originYMm = 25.5f;
    original.originZMm = 3.0f;
    original.gridSpacingMm = 42.0f;
    original.showGrid = false;

    BedHeightmap originalHeightmap;
    originalHeightmap.visible = true;
    originalHeightmap.resize(12, 6);
    for (size_t i = 0; i < originalHeightmap.elevationsMm.size(); ++i) {
        originalHeightmap.elevationsMm[i] = static_cast<float>(i) * 0.1f - 1.0f;
    }

    const std::string path = "bed_io_test_tmp.bed";
    check(saveBedSettings(path, original, originalHeightmap), "BedIO: save succeeds");

    BedSettings loaded;
    BedHeightmap loadedHeightmap;
    check(loadBedSettings(path, loaded, loadedHeightmap), "BedIO: load succeeds");

    checkNear(loaded.widthMm, original.widthMm, "BedIO: width round-trips");
    checkNear(loaded.depthMm, original.depthMm, "BedIO: depth round-trips");
    checkNear(loaded.originXMm, original.originXMm, "BedIO: originX round-trips");
    checkNear(loaded.originYMm, original.originYMm, "BedIO: originY round-trips");
    checkNear(loaded.originZMm, original.originZMm, "BedIO: originZ round-trips");
    checkNear(loaded.gridSpacingMm, original.gridSpacingMm, "BedIO: gridSpacing round-trips");
    check(loaded.showGrid == original.showGrid, "BedIO: showGrid round-trips");

    check(loadedHeightmap.visible == originalHeightmap.visible, "BedIO: heightmap visible round-trips");
    check(loadedHeightmap.cols == originalHeightmap.cols, "BedIO: heightmap cols round-trips");
    check(loadedHeightmap.rows == originalHeightmap.rows, "BedIO: heightmap rows round-trips");
    bool valuesMatch = loadedHeightmap.elevationsMm.size() == originalHeightmap.elevationsMm.size();
    if (valuesMatch) {
        for (size_t i = 0; i < originalHeightmap.elevationsMm.size(); ++i) {
            if (std::abs(loadedHeightmap.elevationsMm[i] - originalHeightmap.elevationsMm[i]) > 1e-4) {
                valuesMatch = false;
                break;
            }
        }
    }
    check(valuesMatch, "BedIO: heightmap elevation values round-trip");

    std::remove(path.c_str());

    BedSettings missing;
    BedHeightmap missingHeightmap;
    check(!loadBedSettings("this_file_does_not_exist.bed", missing, missingHeightmap), "BedIO: loading a missing file returns false");
}

// Gizmo drag math: an orthographic projection with world X in [-100,100]
// mapping to screen X in [0,200] means a ray through screen x=150 passes
// through world x=50 for every point along its length (it travels along Z
// only, at fixed X). The closest point on the world X-axis to that ray
// should therefore land at exactly t=50, regardless of the ray's Z range.
void testGizmoMath() {
    glm::mat4 projection = glm::ortho(-100.0f, 100.0f, -100.0f, 100.0f, -100.0f, 100.0f);

    Ray ray = unprojectRay(projection, glm::vec2(150.0f, 100.0f), 200.0f, 200.0f);
    checkNear(ray.direction.x, 0.0, "Gizmo: ray through screen center-ish column travels along Z (x component ~0)");
    checkNear(ray.direction.y, 0.0, "Gizmo: same ray has no Y component");

    auto t = closestPointOnAxisToRay(glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f), ray);
    check(t.has_value(), "Gizmo: closest point exists for a non-parallel ray/axis pair");
    if (t) checkNear(*t, 50.0, "Gizmo: closest point on the X-axis lands at world x=50, matching the ray's fixed X");

    // A ray parallel to the axis it's being tested against has no unique
    // closest point -- must report that honestly instead of returning a
    // nonsense value.
    Ray parallelRay{glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f)};
    auto none = closestPointOnAxisToRay(glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f), parallelRay);
    check(!none.has_value(), "Gizmo: a ray parallel to the axis correctly returns no unique closest point");

    // Axis picking: three segments meeting at screen origin, pick should
    // find whichever one the point is actually near.
    std::vector<GizmoAxisScreenSegment> segments = {
        {GizmoAxis::X, glm::vec2(0, 0), glm::vec2(100, 0)},
        {GizmoAxis::Y, glm::vec2(0, 0), glm::vec2(0, 100)},
        {GizmoAxis::Z, glm::vec2(0, 0), glm::vec2(50, 50)},
    };
    auto hitX = pickGizmoAxis(segments, glm::vec2(50, 2), 10.0f);
    check(hitX.has_value() && *hitX == GizmoAxis::X, "Gizmo: point near the X arrow picks GizmoAxis::X");
    auto hitNone = pickGizmoAxis(segments, glm::vec2(200, 200), 10.0f);
    check(!hitNone.has_value(), "Gizmo: a point far from every arrow picks nothing");
}

// inverseTransformDelta must undo applyTransform's rotation+flip exactly,
// for any transform -- verified here with a 90-degree rotation plus a
// flip, since that's the case most likely to reveal a sign-convention bug.
void testTransformDelta() {
    Transform t;
    t.rotZDegrees = 90.0;
    t.flipX = true;
    t.x = 500.0; // translation should NOT matter for a delta
    t.y = -200.0;

    glm::dvec3 localDelta(1.0, 2.0, 3.0);
    glm::dvec3 worldA = applyTransform(t, glm::dvec3(0.0));
    glm::dvec3 worldB = applyTransform(t, localDelta);
    glm::dvec3 worldDelta = worldB - worldA;

    glm::dvec3 recovered = inverseTransformDelta(t, worldDelta);
    checkNear(recovered.x, localDelta.x, "TransformDelta: inverse recovers local X through rotation+flip");
    checkNear(recovered.y, localDelta.y, "TransformDelta: inverse recovers local Y through rotation+flip");
    checkNear(recovered.z, localDelta.z, "TransformDelta: inverse recovers local Z (unaffected by Z-only rotation)");
}

// computeGizmoOrigin must react to selection and mode, not just sit at the
// object's raw (and possibly geometry-irrelevant) transform pivot.
void testGizmoOrigin() {
    SceneObject object;
    object.name = "gizmoTest";
    Path p1; p1.number = 1; p1.type = PathType::Print;
    p1.from = glm::dvec3(0, 0, 0); p1.to = glm::dvec3(10, 0, 0);
    Path p2; p2.number = 2; p2.type = PathType::Print;
    p2.from = glm::dvec3(10, 0, 0); p2.to = glm::dvec3(10, 10, 0);
    object.paths = {p1, p2};
    // transform.x/y/z deliberately left at 0 -- a realistic case (a
    // freshly-loaded file) where the raw pivot would NOT be anywhere near
    // this geometry if it were far from local-space origin.

    auto wholeOrigin = computeGizmoOrigin(object, GizmoTargetMode::Object);
    check(wholeOrigin.has_value(), "GizmoOrigin: Object mode returns a value for a non-empty object");
    if (wholeOrigin) {
        // centroid of all 4 endpoints: (0,0,0),(10,0,0),(10,0,0),(10,10,0) -> (7.5, 2.5, 0)
        checkNear(wholeOrigin->x, 7.5, "GizmoOrigin: Object mode centroid X");
        checkNear(wholeOrigin->y, 2.5, "GizmoOrigin: Object mode centroid Y");
    }

    object.selectedPaths = {2};
    auto startOrigin = computeGizmoOrigin(object, GizmoTargetMode::Start);
    check(startOrigin.has_value(), "GizmoOrigin: Start mode returns a value with a selection");
    if (startOrigin) {
        checkNear(startOrigin->x, 10.0, "GizmoOrigin: Start mode uses path 2's FROM point (10,0,0), not its TO");
        checkNear(startOrigin->y, 0.0, "GizmoOrigin: Start mode Y matches path 2's FROM");
    }

    auto endOrigin = computeGizmoOrigin(object, GizmoTargetMode::End);
    if (endOrigin) {
        checkNear(endOrigin->x, 10.0, "GizmoOrigin: End mode uses path 2's TO point (10,10,0)");
        checkNear(endOrigin->y, 10.0, "GizmoOrigin: End mode Y matches path 2's TO");
    }

    object.selectedPaths.clear();
    auto fallbackOrigin = computeGizmoOrigin(object, GizmoTargetMode::Start);
    check(fallbackOrigin.has_value() && std::abs(fallbackOrigin->x - 7.5) < 1e-6,
          "GizmoOrigin: Start mode with an EMPTY selection falls back to the whole-object centroid");
}

// Three connected paths, 1->2->3 (path[i].to == path[i+1].from exactly).
// Selecting only the MIDDLE path and dragging must pull the touching
// endpoint of each unselected neighbor along too -- this is the fix for
// "when I move a path it should stay connected, not move alone."
SceneObject threeConnectedPaths() {
    SceneObject object;
    object.name = "chain";
    Path p1; p1.number = 1; p1.type = PathType::Print; p1.from = glm::dvec3(0, 0, 0); p1.to = glm::dvec3(10, 0, 0);
    Path p2; p2.number = 2; p2.type = PathType::Print; p2.from = glm::dvec3(10, 0, 0); p2.to = glm::dvec3(20, 0, 0);
    Path p3; p3.number = 3; p3.type = PathType::Print; p3.from = glm::dvec3(20, 0, 0); p3.to = glm::dvec3(30, 0, 0);
    object.paths = {p1, p2, p3};
    return object;
}

void testConnectedDragWhole() {
    SceneObject object = threeConnectedPaths();
    object.selectedPaths = {2};

    auto snapshots = buildDragSnapshots(object, GizmoTargetMode::Whole);
    check(snapshots.size() == 3, "ConnectedDrag: Whole mode on the middle path pulls in both neighbors (3 total)");

    std::map<int, PathDragSnapshot> byNumber;
    for (const auto& s : snapshots) byNumber[s.pathNumber] = s;

    check(byNumber.count(1) && byNumber[1].moveFrom == false && byNumber[1].moveTo == true,
          "ConnectedDrag: neighbor path 1 only moves its TO (the end touching path 2)");
    check(byNumber.count(2) && byNumber[2].moveFrom == true && byNumber[2].moveTo == true,
          "ConnectedDrag: the selected path 2 moves both endpoints (Whole mode)");
    check(byNumber.count(3) && byNumber[3].moveFrom == true && byNumber[3].moveTo == false,
          "ConnectedDrag: neighbor path 3 only moves its FROM (the end touching path 2)");
}

void testConnectedDragStart() {
    SceneObject object = threeConnectedPaths();
    object.selectedPaths = {2};

    // Start mode only moves path 2's FROM -- so only path 1 (touching that
    // end) should be pulled in; path 3 (touching path 2's untouched TO)
    // must NOT be affected.
    auto snapshots = buildDragSnapshots(object, GizmoTargetMode::Start);
    check(snapshots.size() == 2, "ConnectedDrag: Start mode only pulls in the neighbor on the moving end (2 total)");

    std::map<int, PathDragSnapshot> byNumber;
    for (const auto& s : snapshots) byNumber[s.pathNumber] = s;
    check(byNumber.count(1) && byNumber[1].moveTo == true, "ConnectedDrag: Start mode still pulls path 1's TO along");
    check(byNumber.count(2) && byNumber[2].moveFrom == true && byNumber[2].moveTo == false,
          "ConnectedDrag: Start mode moves only path 2's FROM");
    check(byNumber.find(3) == byNumber.end(), "ConnectedDrag: Start mode does NOT touch path 3 (unrelated end)");
}

void testConnectedDragGap() {
    // Same chain, but path 3 is disconnected from path 2 (a real gap, not
    // just a direction change) -- dragging path 2's TO in Whole mode must
    // NOT drag path 3 along, since they were never actually touching.
    SceneObject object = threeConnectedPaths();
    object.paths[2].from = glm::dvec3(25, 5, 0); // no longer coincides with path 2's TO (20,0,0)
    object.selectedPaths = {2};

    auto snapshots = buildDragSnapshots(object, GizmoTargetMode::Whole);
    std::map<int, PathDragSnapshot> byNumber;
    for (const auto& s : snapshots) byNumber[s.pathNumber] = s;
    check(byNumber.find(3) == byNumber.end(), "ConnectedDrag: a genuine gap (not touching) does NOT pull the neighbor in");
}

void testFrameBounds() {
    SceneObject object = threeConnectedPaths(); // spans world X 0..30, Y=0, Z=0
    Scene scene;
    scene.addObject(object);
    SceneObject& stored = scene.objects.back();

    // Frame-all (no selection): should cover the whole chain, center at
    // roughly (15, 0, 0), the chain's midpoint.
    auto allBounds = computeFrameBounds(scene, /*preferSelection=*/true);
    check(allBounds.has_value(), "FrameBounds: frame-all returns a value for a non-empty scene");
    if (allBounds) checkNear(allBounds->center.x, 15.0, "FrameBounds: frame-all centers on the whole chain's midpoint");

    // Select just path 1 (world X 0..10): frame-selection should center
    // on ~5, not the whole chain's ~15.
    stored.selectedPaths = {1};
    auto selBounds = computeFrameBounds(scene, /*preferSelection=*/true);
    check(selBounds.has_value(), "FrameBounds: frame-selection returns a value with a selection");
    if (selBounds) checkNear(selBounds->center.x, 5.0, "FrameBounds: frame-selection centers on ONLY the selected path, not the whole chain");

    // preferSelection=false should ignore the selection and frame everything.
    auto forcedAll = computeFrameBounds(scene, /*preferSelection=*/false);
    if (forcedAll) checkNear(forcedAll->center.x, 15.0, "FrameBounds: preferSelection=false frames everything regardless of selection");
}

// Same 7-path snippet as testSrcParser, used here to verify the exporter:
// (1) a completely untouched round-trip produces byte-identical output
// and zero inserted lines, (2) a transform edit patches coordinates and
// nothing else, (3) a speed override on ONE path produces exactly two
// insertions -- the override itself and the automatic "restore" once the
// next unmodified path's original speed no longer matches what we last
// declared.
std::vector<std::string> sampleSrcLinesForExport() {
    return {
        "DEF Chair_01()",
        ";TRAVEL START",
        "$VEL.CP = 0.100",
        "PTP {X 1000,Y 500,Z 300}",
        "LIN {X 1000,Y 500,Z 82,A 90,B 0,C 180}",
        ";TRAVEL END",
        "$VEL.CP = 0.040",
        "LIN {X 1010,Y 500,Z 82}",
        "LIN {X 1020,Y 510,Z 82}",
        "LIN {X 1030,Y 520,Z 82}",
        "LIN {X 1030,Y 520,Z 84}",
        "LIN {X 1040,Y 530,Z 84}",
        "END",
    };
}

void testSrcExporterRoundTrip() {
    std::vector<std::string> lines = sampleSrcLinesForExport();
    SceneObject object = parseSrc("Chair_01", lines);

    ExportResult result;
    std::vector<std::string> exported = buildExportedLines(object, result);

    check(result.success, "SrcExporter: untouched round-trip succeeds");
    check(result.patchedCoordinateLines == 0, "SrcExporter: untouched round-trip patches zero coordinate lines");
    check(result.insertedSpeedLines == 0, "SrcExporter: untouched round-trip inserts zero speed lines");
    check(exported.size() == lines.size(), "SrcExporter: untouched round-trip doesn't change line count");
    bool identical = (exported == lines);
    check(identical, "SrcExporter: untouched round-trip is byte-identical to the original");
}

void testSrcExporterTransform() {
    std::vector<std::string> lines = sampleSrcLinesForExport();
    SceneObject object = parseSrc("Chair_01", lines);
    object.transform.x = 100.0; // pure translation, easy to verify by hand

    ExportResult result;
    std::vector<std::string> exported = buildExportedLines(object, result);

    check(result.success, "SrcExporter: transform export succeeds");
    check(result.patchedCoordinateLines == object.paths.size(),
          "SrcExporter: transform edit patches every path's coordinate line");

    SceneObject reparsed = parseSrc("Chair_01_reparsed", exported);
    check(reparsed.paths.size() == object.paths.size(), "SrcExporter: re-parsed export has the same path count");
    if (reparsed.paths.size() == object.paths.size()) {
        for (size_t i = 0; i < object.paths.size(); ++i) {
            checkNear(reparsed.paths[i].to.x, object.paths[i].to.x + 100.0,
                      "SrcExporter: re-parsed path X reflects the +100 transform");
        }
        // A/B/C on the one line that had them must survive untouched.
        check(reparsed.paths[1].a.has_value() && std::abs(*reparsed.paths[1].a - 90.0) < 1e-6,
              "SrcExporter: orientation (A) survives a coordinate patch on the same line");
    }
}

void testSrcExporterSpeedOverride() {
    std::vector<std::string> lines = sampleSrcLinesForExport();
    SceneObject object = parseSrc("Chair_01", lines);

    // Override path 4 (0-indexed 3, the 2nd print path -- both at the
    // original 0.040) to a slower speed. Expect exactly two insertions:
    // the override before path 4, and an automatic restore to 0.040
    // before path 5 (the next path, whose OWN original speed diverges
    // from what we just declared).
    check(object.paths.size() == 7, "SrcExporter: sample still parses to 7 paths (precondition)");
    object.paths[3].speedOverride = 0.020;

    ExportResult result;
    std::vector<std::string> exported = buildExportedLines(object, result);

    check(result.success, "SrcExporter: speed-override export succeeds");
    check(result.insertedSpeedLines == 2,
          "SrcExporter: a single-path override produces exactly 2 insertions (override + auto-restore)");

    std::string joined;
    for (const auto& line : exported) joined += line + "\n";
    check(joined.find("$VEL.CP = 0.020000") != std::string::npos, "SrcExporter: override speed line is present");
    check(joined.find("$VEL.CP = 0.040000") != std::string::npos, "SrcExporter: restore speed line is present");

    SceneObject reparsed = parseSrc("Chair_01_reparsed", exported);
    if (reparsed.paths.size() == 7) {
        checkNear(reparsed.paths[3].speed.value_or(-1.0), 0.020, "SrcExporter: re-parsed path 4 picks up the overridden speed");
        checkNear(reparsed.paths[4].speed.value_or(-1.0), 0.040, "SrcExporter: re-parsed path 5 picks up the restored speed");
    }
}

void testSrcExporterLayerAction() {
    std::vector<std::string> lines = sampleSrcLinesForExport();
    SceneObject object = parseSrc("Chair_01", lines);

    LayerAction action;
    action.layer = 1;
    action.label = "Part cooling ON";
    action.krlText = "$OUT[12] = TRUE";
    object.layerActions.push_back(action);

    ExportResult result;
    std::vector<std::string> exported = buildExportedLines(object, result);

    check(result.success, "SrcExporter: layer-action export succeeds");
    check(result.insertedLayerActions == 1, "SrcExporter: exactly one layer action inserted");

    std::string joined;
    for (const auto& line : exported) joined += line + "\n";
    check(joined.find("$OUT[12] = TRUE") != std::string::npos, "SrcExporter: layer action's KRL text is present in the output");
    check(joined.find("Part cooling ON") != std::string::npos, "SrcExporter: layer action's label is present as a traceability comment");
}

// Splits path index 3 (LIN {X 1020,Y 510,Z 82}) and checks the model
// side only: path count, which path keeps its original number/srcLine,
// and where the new synthetic path lands in the vector.
void testPathSplitModel() {
    std::vector<std::string> lines = sampleSrcLinesForExport();
    SceneObject object = parseSrc("Chair_01", lines);

    size_t originalCount = object.paths.size();
    int targetNumber = object.paths[3].number;
    glm::dvec3 originalFrom = object.paths[3].from;
    glm::dvec3 originalTo = object.paths[3].to;
    int originalSrcLine = object.paths[3].srcLine;

    object.selectedPaths.insert(targetNumber);
    splitSelectedPaths(object);

    check(object.paths.size() == originalCount + 1, "PathSplit: splitting one path adds exactly one path");

    auto it = std::find_if(object.paths.begin(), object.paths.end(),
                            [&](const Path& p) { return p.number == targetNumber; });
    check(it != object.paths.end(), "PathSplit: the original path number still exists after split");
    if (it == object.paths.end()) return;

    glm::dvec3 midpoint = (originalFrom + originalTo) * 0.5;
    checkNear(it->from.x, midpoint.x, "PathSplit: original path's FROM moved to the midpoint (X)");
    checkNear(it->to.x, originalTo.x, "PathSplit: original path's TO is unchanged (still the real endpoint)");
    check(it->srcLine == originalSrcLine, "PathSplit: original path keeps its own srcLine");

    check(it != object.paths.begin(), "PathSplit: a new path precedes the original one in the vector");
    if (it == object.paths.begin()) return;

    const Path& newHalf = *(it - 1);
    checkNear(newHalf.from.x, originalFrom.x, "PathSplit: new first-half path's FROM is the original FROM");
    checkNear(newHalf.to.x, midpoint.x, "PathSplit: new first-half path's TO is the midpoint");
    check(newHalf.number != targetNumber, "PathSplit: new first-half path got a distinct number");
    check(newHalf.srcLine == -1, "PathSplit: new first-half path has no source line of its own");
    check(newHalf.cloneTemplateSrcLine == originalSrcLine, "PathSplit: new first-half path's clone template points at the original srcLine");
}

// Same split, but through the exporter: confirms the synthetic path
// actually produces a real motion line in the exported file, not a
// silently-dropped vertex.
void testPathSplitExport() {
    std::vector<std::string> lines = sampleSrcLinesForExport();
    SceneObject object = parseSrc("Chair_01", lines);

    int targetNumber = object.paths[3].number;
    glm::dvec3 originalFrom = object.paths[3].from;
    glm::dvec3 originalTo = object.paths[3].to;
    object.selectedPaths.insert(targetNumber);
    splitSelectedPaths(object);

    ExportResult result;
    std::vector<std::string> exported = buildExportedLines(object, result);

    check(result.success, "SrcExporter: split-path export succeeds");
    check(exported.size() == lines.size() + 1, "SrcExporter: splitting one path inserts exactly one new line");

    SceneObject reparsed = parseSrc("Chair_01_split_reparsed", exported);
    check(reparsed.paths.size() == object.paths.size(),
          "SrcExporter: re-parsed split export has the same path count as the in-memory split");

    glm::dvec3 midpoint = (originalFrom + originalTo) * 0.5;
    bool foundMidpointVertex = false;
    bool foundOriginalEndpoint = false;
    for (const auto& p : reparsed.paths) {
        if (glm::length(p.to - midpoint) < 1e-6) foundMidpointVertex = true;
        if (glm::length(p.to - originalTo) < 1e-6) foundOriginalEndpoint = true;
    }
    check(foundMidpointVertex, "SrcExporter: re-parsed split export contains a motion line ending exactly at the midpoint");
    check(foundOriginalEndpoint, "SrcExporter: re-parsed split export still reaches the original endpoint");
}

// Two objects, both using the sample file, object B offset by (500, 0, 0)
// so its world-space first point is unambiguous. Links A -> B and checks
// the world-space preview matches A's last path's WORLD end point and
// B's first path's WORLD start point.
void testObjectLinkPreview() {
    Scene scene;
    SceneObject a = parseSrc("A", sampleSrcLinesForExport());
    SceneObject b = parseSrc("B", sampleSrcLinesForExport());
    b.transform.x = 500.0;
    int idA = scene.addObject(a).id;
    int idB = scene.addObject(b).id;
    scene.toggleLink(idA, idB);

    std::vector<LinkPreview> previews = computeLinkPreviews(scene);
    check(previews.size() == 1, "ObjectLinking: one preview for one linked pair");
    if (previews.size() != 1) return;

    SceneObject* objA = scene.findObject(idA);
    SceneObject* objB = scene.findObject(idB);
    glm::dvec3 expectedFrom = applyTransform(objA->transform, objA->paths.back().to);
    glm::dvec3 expectedTo = applyTransform(objB->transform, objB->paths.front().from);
    checkNear(previews[0].worldFrom.x, expectedFrom.x, "ObjectLinking: preview FROM matches A's last path's world end point (X)");
    checkNear(previews[0].worldTo.x, expectedTo.x, "ObjectLinking: preview TO matches B's first path's world start point (X)");
    // B's first path's local X starts near 0 (untransformed), so its
    // world X after the +500 transform should land close to 500 --
    // confirms the transform is actually being applied, not skipped.
    check(previews[0].worldTo.x > 400.0, "ObjectLinking: preview TO reflects B's +500 transform (not still near local-space 0)");
}

// Bakes the same A -> B link and verifies it becomes a real, permanent,
// exportable path -- not just a viewport line.
void testObjectLinkBake() {
    Scene scene;
    SceneObject a = parseSrc("A", sampleSrcLinesForExport());
    SceneObject b = parseSrc("B", sampleSrcLinesForExport());
    b.transform.x = 500.0;
    int idA = scene.addObject(a).id;
    int idB = scene.addObject(b).id;
    scene.toggleLink(idA, idB);

    SceneObject* objA = scene.findObject(idA);
    SceneObject* objB = scene.findObject(idB);
    size_t originalPathCount = objA->paths.size();
    size_t originalLineCount = objA->sourceLines.size();
    glm::dvec3 expectedWorldTo = applyTransform(objB->transform, objB->paths.front().from);

    bool baked = bakeLinkToTravel(scene, idA, idB);
    check(baked, "ObjectLinking: bake succeeds");
    check(scene.objectLinks.empty(), "ObjectLinking: baking removes the pair from scene.objectLinks");
    check(objA->paths.size() == originalPathCount + 1, "ObjectLinking: baking adds exactly one path to the from-object");
    check(objA->sourceLines.size() == originalLineCount + 1, "ObjectLinking: baking adds exactly one source line to the from-object");

    const Path& baked_travel = objA->paths.back();
    check(baked_travel.type == PathType::Travel, "ObjectLinking: baked path is a Travel move");
    check(baked_travel.srcLine >= 0, "ObjectLinking: baked path has a REAL srcLine, not a synthetic one");

    glm::dvec3 exportedWorldTo = applyTransform(objA->transform, baked_travel.to);
    checkNear(exportedWorldTo.x, expectedWorldTo.x, "ObjectLinking: baked path's world-space TO matches B's original start point (X)");

    // The whole point of giving it a real srcLine: it must round-trip
    // through the exporter and re-parser exactly like any other path.
    ExportResult result;
    std::vector<std::string> exported = buildExportedLines(*objA, result);
    check(result.success, "ObjectLinking: exporting the from-object after baking succeeds");
    SceneObject reparsed = parseSrc("A_reparsed", exported);
    check(reparsed.paths.size() == objA->paths.size(), "ObjectLinking: re-parsed export has the baked path too");
    if (!reparsed.paths.empty()) {
        checkNear(reparsed.paths.back().to.x, baked_travel.to.x, "ObjectLinking: re-parsed export's last path matches the baked travel's local X");
    }
}

// A 2x2 heightmap with elevation 0 at the low-X edge and 10 at the
// high-X edge (no Y gradient), over a 1000x1000mm bed centered at the
// world origin -- so world X directly maps to a known, hand-computable
// elevation via bilinear interpolation.
void testBedConformSampling() {
    BedSettings bed;
    bed.widthMm = 1000.0f;
    bed.depthMm = 1000.0f;

    BedHeightmap heightmap;
    heightmap.resize(2, 2);
    heightmap.at(0, 0) = 0.0f;  // local (0,0) -> world (-500,-500)
    heightmap.at(1, 0) = 10.0f; // local (1000,0) -> world (500,-500)
    heightmap.at(0, 1) = 0.0f;  // local (0,1000) -> world (-500,500)
    heightmap.at(1, 1) = 10.0f; // local (1000,1000) -> world (500,500)

    checkNear(sampleBedElevation(heightmap, bed, -500.0, -500.0), 0.0, "BedConform: sample at a corner matches its exact value (low)");
    checkNear(sampleBedElevation(heightmap, bed, 500.0, 500.0), 10.0, "BedConform: sample at a corner matches its exact value (high)");
    checkNear(sampleBedElevation(heightmap, bed, 0.0, 0.0), 5.0, "BedConform: sample at the center is the bilinear average");
    checkNear(sampleBedElevation(heightmap, bed, 0.0, -500.0), 5.0, "BedConform: sample varies with X only on this grid, independent of Y");
}

// Two connected layer-1 print paths (X: -500 -> 0 -> 500, elevation
// 0 -> 5 -> 10) plus one layer-2 path, using the same heightmap as
// above. Checks Z shift, that connectivity survives (the shared vertex
// between the two layer-1 paths gets the SAME Z from both sides), the
// speed-override formula, and that layer 2 (beyond affectedLayers=1)
// gets no effect at all.
void testBedConformApply() {
    BedSettings bed;
    bed.widthMm = 1000.0f;
    bed.depthMm = 1000.0f;

    BedHeightmap heightmap;
    heightmap.resize(2, 2);
    heightmap.at(0, 0) = 0.0f;
    heightmap.at(1, 0) = 10.0f;
    heightmap.at(0, 1) = 0.0f;
    heightmap.at(1, 1) = 10.0f;

    SceneObject object;
    object.name = "Test";
    Path p1;
    p1.number = 1;
    p1.type = PathType::Print;
    p1.layer = 1;
    p1.motion = "LIN";
    p1.speed = 0.05;
    p1.from = glm::dvec3(-500.0, 0.0, 0.0);
    p1.to = glm::dvec3(0.0, 0.0, 0.0);
    Path p2;
    p2.number = 2;
    p2.type = PathType::Print;
    p2.layer = 1;
    p2.motion = "LIN";
    p2.speed = 0.05;
    p2.from = p1.to;
    p2.to = glm::dvec3(500.0, 0.0, 0.0);
    Path p3;
    p3.number = 3;
    p3.type = PathType::Print;
    p3.layer = 2;
    p3.motion = "LIN";
    p3.speed = 0.05;
    p3.from = p2.to;
    p3.to = glm::dvec3(500.0, 100.0, 0.0);
    object.paths = {p1, p2, p3};

    BedConformOptions options;
    options.affectedLayers = 1;
    options.adjustZ = true;
    options.adjustSpeed = true;
    options.speedGainPerMm = 0.1;
    applyBedConform(object, heightmap, bed, options);

    checkNear(object.paths[0].from.z, 0.0, "BedConform: layer-1 path's FROM Z shifted by its own local elevation (0, low edge)");
    checkNear(object.paths[0].to.z, 5.0, "BedConform: layer-1 path's TO Z shifted by its own local elevation (5, center)");
    checkNear(object.paths[1].from.z, object.paths[0].to.z,
               "BedConform: connected paths' shared vertex gets the SAME Z from both sides (connectivity preserved)");
    checkNear(object.paths[1].to.z, 10.0, "BedConform: layer-1 path's TO Z shifted by its own local elevation (10, high edge)");

    check(object.paths[0].speedOverride.has_value(), "BedConform: layer-1 path got a speed override");
    if (object.paths[0].speedOverride.has_value()) {
        checkNear(*object.paths[0].speedOverride, 0.05 * (1.0 + 1.0 * 0.1 * 5.0),
                   "BedConform: speed override matches base*(1 + weight*gain*elevation)");
    }

    checkNear(object.paths[2].to.z, 0.0, "BedConform: a layer beyond affectedLayers gets zero Z shift (taper reached 0)");
    check(!object.paths[2].speedOverride.has_value(), "BedConform: a layer beyond affectedLayers gets no speed override");
}

void testMirrorObject() {
    SceneObject source = parseSrc("Part", sampleSrcLinesForExport());
    SceneObject mirror = mirrorObject(source, 200.0);

    // A plain translated COPY, not a true mirror -- flipping used to be
    // the default and was a real reported safety bug (see
    // testMirrorTransitionApproachesNearEdgeNotFarEdge): it could
    // relocate a layer's first print point to the far side of the copy's
    // own footprint, making the interleave's cross-part transition drag
    // the nozzle across already-deposited material at travel speed.
    check(mirror.transform.flipX == source.transform.flipX, "MirrorObject: the copy does NOT flip (a plain translation, not a mirror)");
    check(mirror.paths.size() == source.paths.size(), "MirrorObject: mirror has the same path count as the source");
    check(mirror.name != source.name, "MirrorObject: mirror gets a distinct name");
    check(mirror.selectedPaths.empty(), "MirrorObject: mirror starts with an empty selection");

    // The mirror must sit entirely clear of the source in X, or the two
    // parts would physically collide on the bed -- the whole point of
    // the "safe distance" argument.
    double sourceMaxX = std::numeric_limits<double>::lowest();
    for (const auto& p : source.paths) {
        sourceMaxX = std::max({sourceMaxX, applyTransform(source.transform, p.from).x, applyTransform(source.transform, p.to).x});
    }
    double mirrorMinX = std::numeric_limits<double>::max();
    for (const auto& p : mirror.paths) {
        mirrorMinX = std::min({mirrorMinX, applyTransform(mirror.transform, p.from).x, applyTransform(mirror.transform, p.to).x});
    }
    check(mirrorMinX >= sourceMaxX, "MirrorObject: mirror is placed entirely clear of the source in X (no overlap)");
}

// Real-use report, found via the print animation: after mirroring and
// interleaving, the cross-part transition into the mirrored copy landed
// on the FAR edge of the copy (the side away from the part it was
// transitioning FROM) instead of the near edge -- meaning the travel had
// to cross the copy's own footprint (material already printed, or about
// to be) at travel speed to get there. A collision/dripping hazard, not
// just a cosmetic one. Root cause: mirrorObject() flips the copy's local
// X, which relocates whichever point happens to be "the layer's first
// point" in file order to a different SIDE of the copy's own bounding
// box -- flipping doesn't reorder the path list, so the print still
// starts at the same FILE-ORDER point, but that point's physical
// position can end up on the opposite side purely because of the flip,
// with no relationship to which side actually faces the neighboring
// part.
void testMirrorTransitionApproachesNearEdgeNotFarEdge() {
    Scene scene;
    // A single-layer object whose print path clearly starts at its own
    // LOW-X corner (X=20) and ends at its HIGH-X corner (X=100) -- local
    // X deliberately doesn't start at 0, matching the note in
    // MirrorObject.cpp about real KUKA files never doing that either.
    std::vector<std::string> lines = {
        "DEF Part()",
        "$VEL.CP = 0.100",
        "LIN {X 20, Y 0, Z 2, A 90, B 0, C 180, E1 0, E2 0, E3 0, E4 0, E5 0, E6 0 } C_VEL",
        "LIN {X 100, Y 0, Z 2, A 90, B 0, C 180, E1 0, E2 0, E3 0, E4 0, E5 0, E6 0 } C_VEL",
        "END",
    };
    SceneObject a = parseSrc("A", lines);
    int idA = scene.addObject(a).id;
    SceneObject b = mirrorObject(*scene.findObject(idA), 50.0);
    int idB = scene.addObject(std::move(b)).id;

    // B sits entirely to the right of A (higher X) -- confirmed by the
    // existing MirrorObject test elsewhere. A transition INTO B should
    // therefore always land on B's own LOW-X (near, left) edge, the side
    // actually facing A, never B's HIGH-X (far, right) edge.
    const SceneObject* bObj = scene.findObject(idB);
    double bMinX = std::numeric_limits<double>::max(), bMaxX = std::numeric_limits<double>::lowest();
    for (const auto& p : bObj->paths) {
        bMinX = std::min({bMinX, applyTransform(bObj->transform, p.from).x, applyTransform(bObj->transform, p.to).x});
        bMaxX = std::max({bMaxX, applyTransform(bObj->transform, p.from).x, applyTransform(bObj->transform, p.to).x});
    }

    InterleaveOptions options;
    options.detourMarginMm = 100.0;
    auto merged = buildInterleavedObject(scene, {idA, idB}, options);
    check(merged.has_value(), "MirrorTransition: interleave succeeds");
    if (!merged.has_value()) return;

    // Find the LAST synthetic cross-part transition travel (a detour, if
    // one fired, is 2-3 hops -- the one that matters is the FINAL landing
    // point right before printing resumes, not an intermediate lane hop)
    // and check which edge of B it landed on.
    bool foundTransition = false;
    double transitionTargetX = 0.0;
    for (const auto& p : merged->paths) {
        if (p.srcLine < 0 || p.srcLine >= static_cast<int>(merged->sourceLines.size())) continue;
        if (merged->sourceLines[static_cast<size_t>(p.srcLine)].find("GCODEFORGE INTERLEAVE TRAVEL") == std::string::npos) continue;
        foundTransition = true;
        transitionTargetX = p.to.x; // keeps overwriting -- last match wins
    }
    check(foundTransition, "MirrorTransition: precondition -- a cross-part transition travel was generated");

    double distToNearEdge = std::abs(transitionTargetX - bMinX);
    double distToFarEdge = std::abs(transitionTargetX - bMaxX);
    check(distToNearEdge < distToFarEdge,
          "MirrorTransition: the transition into the mirrored copy lands on its NEAR edge (facing the previous "
          "part), not its far edge -- landing on the far edge means the travel just crossed the whole copy's own "
          "footprint at travel speed (the reported safety issue)");
}

void testInterleavePrint() {
    Scene scene;
    SceneObject a = parseSrc("A", sampleSrcLinesForExport());
    int idA = scene.addObject(a).id;
    SceneObject b = mirrorObject(*scene.findObject(idA), 200.0);
    int idB = scene.addObject(std::move(b)).id;

    InterleaveOptions options;
    options.detourMarginMm = 100.0;
    options.travelSpeed = 0.5;

    auto merged = buildInterleavedObject(scene, {idA, idB}, options);
    check(merged.has_value(), "InterleavePrint: building an interleaved object from two objects succeeds");
    if (!merged.has_value()) return;

    check(merged->transform.x == 0.0 && merged->transform.y == 0.0,
          "InterleavePrint: merged object's own transform is identity (coordinates are baked to world space)");

    size_t printCount = 0, travelCount = 0;
    for (const auto& p : merged->paths) {
        if (p.type == PathType::Print) ++printCount;
        else ++travelCount;
    }
    check(printCount > 0, "InterleavePrint: merged object has print paths");
    check(travelCount > 0, "InterleavePrint: merged object has generated travel paths between segments");

    // The safety property that actually matters on real hardware: no
    // travel may cross at a height that could clip a part. Every
    // generated cross-part travel either climbs/descends vertically
    // (same XY) or moves horizontally at >= the safe height.
    // Cross-part travels must NOT hop to a clearance height. Interleaving
    // keeps every part at the same layer height, so a horizontal move
    // passes through the empty gap between them -- a lift was pure wasted
    // travel time and an extra stringing opportunity. The only Z change
    // allowed is the layer step itself (going from layer N to layer N+1
    // genuinely has to rise one layer).
    double layerStep = 0.0;
    {
        const auto& L = scene.findObject(idA)->layers;
        if (L.size() >= 2) layerStep = std::abs(L[1].z - L[0].z);
    }
    // Only the SYNTHETIC cross-part transitions this check is actually
    // about -- not the source object's own header/footer travels (the
    // approach down to print start, the retreat away from it before
    // shutdown), which are genuine, deliberate, large Z changes by
    // design and are now also preserved as real travel paths (see
    // editor/Boilerplate.h). Synthetic transitions are the only ones
    // tagged with a GCODEFORGE marker comment on their own source line.
    double worstTravelDz = 0.0;
    for (const auto& p : merged->paths) {
        if (p.type != PathType::Travel) continue;
        if (p.srcLine < 0 || p.srcLine >= static_cast<int>(merged->sourceLines.size())) continue;
        const std::string& line = merged->sourceLines[static_cast<size_t>(p.srcLine)];
        if (line.find("GCODEFORGE INTERLEAVE TRAVEL") == std::string::npos &&
            line.find("GCODEFORGE in-layer reposition") == std::string::npos) continue;
        worstTravelDz = std::max(worstTravelDz, std::abs(p.to.z - p.from.z));
    }
    check(worstTravelDz <= layerStep + 1e-6,
          "InterleavePrint: no SYNTHETIC cross-part travel hops above the layer step (no clearance lift)");

    // Alternation is the whole point: consecutive printed segments must
    // come from different parts. Both parts sit at disjoint X ranges
    // (mirror is placed clear of the source), so the X midpoint of each
    // segment identifies which part it belongs to.
    std::vector<double> segmentMidX;
    int lastLayer = -1;
    for (const auto& p : merged->paths) {
        if (p.type != PathType::Print) continue;
        if (p.layer != lastLayer) {
            segmentMidX.push_back((p.from.x + p.to.x) * 0.5);
            lastLayer = p.layer;
        }
    }
    check(segmentMidX.size() >= 2, "InterleavePrint: merged object contains at least two printed segments");
    bool alternates = true;
    for (size_t i = 1; i < segmentMidX.size(); ++i) {
        if (std::abs(segmentMidX[i] - segmentMidX[i - 1]) < 1.0) alternates = false;
    }
    check(alternates, "InterleavePrint: consecutive printed segments come from different parts (alternating, not sequential)");

    // Must survive the exporter, same as any other object.
    ExportResult result;
    std::vector<std::string> exported = buildExportedLines(*merged, result);
    check(result.success, "InterleavePrint: exporting the merged object succeeds");
    SceneObject reparsed = parseSrc("merged_reparsed", exported);
    check(reparsed.paths.size() == merged->paths.size(),
          "InterleavePrint: re-parsed export has the same path count as the merged object");
}

// An object with FEWER layers must simply drop out of the rotation once
// exhausted, leaving the taller one to finish normally.
void testInterleaveUnevenLayers() {
    Scene scene;
    SceneObject tall = parseSrc("Tall", sampleSrcLinesForExport());
    int idTall = scene.addObject(tall).id;

    SceneObject shortObj = mirrorObject(*scene.findObject(idTall), 200.0);
    // Drop every path above layer 1, leaving a single-layer part.
    shortObj.paths.erase(std::remove_if(shortObj.paths.begin(), shortObj.paths.end(),
                                         [](const Path& p) { return p.type == PathType::Print && p.layer > 1; }),
                          shortObj.paths.end());
    int idShort = scene.addObject(std::move(shortObj)).id;

    InterleaveOptions options;
    options.detourMarginMm = 100.0;

    auto merged = buildInterleavedObject(scene, {idTall, idShort}, options);
    check(merged.has_value(), "InterleavePrint: uneven layer counts still build successfully");
    if (!merged.has_value()) return;

    size_t tallPrintPaths = 0;
    for (const auto& p : scene.findObject(idTall)->paths) {
        if (p.type == PathType::Print) ++tallPrintPaths;
    }
    size_t mergedPrintPaths = 0;
    for (const auto& p : merged->paths) {
        if (p.type == PathType::Print) ++mergedPrintPaths;
    }
    check(mergedPrintPaths > tallPrintPaths,
          "InterleavePrint: merged output includes the taller object's full print plus the shorter one's layers");
}

// The joint-space "first safe position" -- a real Eidos program's startup
// PTP, reproduced verbatim from a production file. It has NO X/Y/Z, only
// A1-A6, which is exactly why the parser used to drop it silently and it
// was impossible to find in the viewport.
void testStartPointJointMove() {
    std::vector<std::string> lines = {
        "DEF Part()",
        "BAS(#BASE,1)",
        "BAS(#VEL_PTP,1) ",
        "PTP {A1 0.000, A2 -89.990, A3 99.400, A4 0.000, A5 -9.410, A6 0.000}",
        "$VEL.CP = 0.061000",
        "LIN {X 291.12, Y 2027.09, Z 4.20, A 164.577, B 90.000, C 164.767} C_VEL",
        "LIN {X 291.12, Y 2027.09, Z 2.20, A 164.577, B 90.000, C 164.767} C_VEL",
        ";travel end",
        "$VEL.CP = 0.060000",
        "LIN {X 291.12, Y 2650.27, Z 2.20, A 164.577, B 90.000, C 164.767} C_VEL",
        "END",
    };
    SceneObject object = parseSrc("Part", lines);

    check(object.startPoint.present, "StartPoint: the joint-space PTP is captured, not silently dropped");
    check(object.startPoint.jointSpace, "StartPoint: it's flagged as joint-space (no Cartesian coords of its own)");
    check(object.startPoint.srcLine == 3, "StartPoint: srcLine points at the actual PTP line");
    checkNear(object.startPoint.joints.a1, 0.0, "StartPoint: A1 parsed");
    checkNear(object.startPoint.joints.a2, -89.990, "StartPoint: A2 parsed (negative value)");
    checkNear(object.startPoint.joints.a3, 99.400, "StartPoint: A3 parsed");
    checkNear(object.startPoint.joints.a5, -9.410, "StartPoint: A5 parsed");

    // The A1..A6 regexes must not be confused by the bare A/B/C tool
    // orientation on the Cartesian lines -- a naive \bA match would read
    // "A 164.577" as A1.
    checkNear(object.startPoint.joints.a4, 0.0, "StartPoint: A4 is 0, not the Cartesian lines' A 164.577");

    check(object.startPoint.position.has_value(), "StartPoint: got a display anchor from the first Cartesian point");
    if (object.startPoint.position.has_value()) {
        checkNear(object.startPoint.position->x, 291.12, "StartPoint: anchor X is the first Cartesian point's X");
        checkNear(object.startPoint.position->z, 4.20, "StartPoint: anchor Z is the first Cartesian point's Z");
    }

    // Capturing it must not have changed how the real motion paths parse.
    check(object.paths.size() == 3, "StartPoint: the joint move is NOT counted as a motion path");
    check(object.paths.front().type == PathType::Travel, "StartPoint: first Cartesian move is still a travel");

    // And it must still export byte-identically -- the joint move is
    // preserved as an untouched source line, not rewritten.
    ExportResult result;
    std::vector<std::string> exported = buildExportedLines(object, result);
    check(exported == lines, "StartPoint: a file with a joint-space PTP still round-trips byte-identically");
}

// Travels must be selectable, splittable and speed-editable as a group --
// the layer table can never reach them (they carry no layer), so these
// selectors are the only way in.
void testTravelSelectionAndSpeed() {
    SceneObject object = parseSrc("Part", sampleSrcLinesForExport());

    std::vector<int> travels = travelPathNumbers(object);
    std::vector<int> prints = printPathNumbers(object);
    check(!travels.empty(), "TravelEdit: the sample has travel paths to select");
    check(!prints.empty(), "TravelEdit: the sample has print paths to select");
    check(travels.size() + prints.size() == object.paths.size(),
          "TravelEdit: every path is either a travel or a print, none double-counted");

    for (int n : travels) {
        const Path* p = object.findPath(n);
        check(p && p->type == PathType::Travel, "TravelEdit: travelPathNumbers returns only travels");
        if (!p || p->type != PathType::Travel) break;
    }

    // Speed edits must actually land on travels -- only PTP is skipped.
    SpeedApplyResult result = applySpeedToPaths(object, travels, SpeedApplyMode::Exact, 0.25);
    check(result.appliedCount > 0, "TravelEdit: applying a speed to travels changes at least one");
    for (int n : travels) {
        const Path* p = object.findPath(n);
        if (!p || p->motion == "PTP") continue; // PTP is correctly skipped
        checkNear(p->effectiveSpeed(), 0.25, "TravelEdit: a non-PTP travel picks up the new speed");
    }

    // And a travel must be splittable, same as a print path.
    SceneObject splitTarget = parseSrc("Part2", sampleSrcLinesForExport());
    std::vector<int> splitTravels = travelPathNumbers(splitTarget);
    int firstTravel = splitTravels.front();
    const Path* before = splitTarget.findPath(firstTravel);
    glm::dvec3 originalFrom = before->from;
    glm::dvec3 originalTo = before->to;
    size_t originalCount = splitTarget.paths.size();

    splitTarget.selectedPaths.insert(firstTravel);
    splitSelectedPaths(splitTarget);
    check(splitTarget.paths.size() == originalCount + 1, "TravelEdit: splitting a travel adds one path");

    const Path* after = splitTarget.findPath(firstTravel);
    glm::dvec3 midpoint = (originalFrom + originalTo) * 0.5;
    checkNear(after->from.x, midpoint.x, "TravelEdit: split travel's second half starts at the midpoint");
    checkNear(after->to.x, originalTo.x, "TravelEdit: split travel's second half still ends at the original endpoint");
}

// The measured safe point is a CELL property: it must survive a bed
// save/load round-trip, since re-reading it off the pendant for every
// job is exactly the friction it exists to remove. Uses the real values
// measured on the operator's KR 120 R3100-2.
void testSafePointRoundTrip() {
    BedSettings bed;
    bed.safePointMeasured = true;
    bed.safePointXMm = 970.7f;
    bed.safePointYMm = 1760.8f;
    bed.safePointZMm = 1005.0f;

    BedHeightmap heightmap;
    const std::string path = "safepoint_test_tmp.bed";
    check(saveBedSettings(path, bed, heightmap), "SafePoint: bed save succeeds");

    BedSettings loaded;
    BedHeightmap loadedHeightmap;
    check(loadBedSettings(path, loaded, loadedHeightmap), "SafePoint: bed load succeeds");
    check(loaded.safePointMeasured, "SafePoint: measured flag round-trips");
    // Compare float-to-float, not float-to-double-literal: float(970.7)
    // differs from the double literal 970.7 by ~1.2e-5, which checkNear's
    // 1e-6 tolerance rejects. That's a property of float, not a
    // round-trip failure -- and "loaded matches what I saved" is the
    // assertion that actually means something here anyway.
    checkNear(loaded.safePointXMm, bed.safePointXMm, "SafePoint: X round-trips");
    checkNear(loaded.safePointYMm, bed.safePointYMm, "SafePoint: Y round-trips");
    checkNear(loaded.safePointZMm, bed.safePointZMm, "SafePoint: Z round-trips");

    std::remove(path.c_str());

    // An un-measured bed must NOT come back claiming to be measured --
    // that would draw a marker at (0,0,0) and look authoritative.
    BedSettings unmeasured;
    BedHeightmap h2;
    check(saveBedSettings(path, unmeasured, h2), "SafePoint: saving an unmeasured bed succeeds");
    BedSettings loaded2;
    BedHeightmap h3;
    check(loadBedSettings(path, loaded2, h3), "SafePoint: loading an unmeasured bed succeeds");
    check(!loaded2.safePointMeasured, "SafePoint: an unmeasured bed stays unmeasured after round-trip");
    std::remove(path.c_str());
}

// Mirror + interleave as ONE operation, and the layer-action carry-through
// that used to silently drop part-cooling commands.
// Reported from real use: "after the file is exported the speed is 0."
// Checks the merged object's actual Path::speed values survive the
// mirror+interleave pipeline -- not just that layerActions do (the
// existing test only checked that).
void testMirrorAndInterleaveKeepsSpeed() {
    Scene scene;
    SceneObject part = parseSrc("Part", sampleSrcLinesForExport());

    // Confirm the SOURCE actually has real, nonzero speeds before doing
    // anything else -- otherwise a failure here would be ambiguous about
    // where the value was lost.
    bool sourceHasRealSpeed = false;
    for (const auto& p : part.paths) {
        if (p.type == PathType::Print && p.effectiveSpeed() > 1e-6) sourceHasRealSpeed = true;
    }
    check(sourceHasRealSpeed, "MirrorInterleaveSpeed: precondition -- the source object has real nonzero speeds");

    int sourceId = scene.addObject(std::move(part)).id;

    MirrorInterleaveOptions options;
    options.copies = 2;
    options.gapMm = 200.0;

    auto merged = mirrorAndInterleave(scene, sourceId, options);
    check(merged.has_value(), "MirrorInterleaveSpeed: mirror+interleave succeeds");
    if (!merged.has_value()) return;

    int zeroSpeedPrints = 0, totalPrints = 0;
    for (const auto& p : merged->paths) {
        if (p.type != PathType::Print) continue;
        ++totalPrints;
        if (p.effectiveSpeed() <= 1e-6) ++zeroSpeedPrints;
    }
    check(totalPrints > 0, "MirrorInterleaveSpeed: merged object has print paths to check");
    check(zeroSpeedPrints == 0,
          "MirrorInterleaveSpeed: NO merged print path has speed 0 (the reported bug)");

    // And that it actually reaches the exported KRL text as a real
    // $VEL.CP value, not silently defaulted.
    ExportResult result;
    std::vector<std::string> exported = buildExportedLines(*merged, result);
    check(result.success, "MirrorInterleaveSpeed: merged program exports");
    bool foundNonZeroVelCp = false;
    for (const auto& line : exported) {
        auto pos = line.find("$VEL.CP");
        if (pos == std::string::npos) continue;
        auto eq = line.find('=', pos);
        if (eq == std::string::npos) continue;
        if (std::abs(std::stod(line.substr(eq + 1))) > 1e-6) foundNonZeroVelCp = true;
    }
    check(foundNonZeroVelCp, "MirrorInterleaveSpeed: exported file contains a real, nonzero $VEL.CP");
}

// Reported from real use: a 4-copy mirror+interleave export was rejected
// by the web editor's own structural validator (1630 CRITICAL issues) and
// its points failed to load on the KUKA pendant. Root cause: synthetic
// cross-part travel and in-layer reposition lines carried only X/Y/Z,
// dropping tool orientation (A/B/C) and all six extruder axes (E1-E6),
// which every real motion line in the format carries. Uses 3 copies so
// the far-to-near transition is forced through the Y-detour path too
// (segmentCrossesFootprint), exercising both synthetic-line call sites.
void testInterleaveTravelsKeepFullAxisSet() {
    Scene scene;
    std::vector<std::string> fullAxisLines = {
        "DEF Part()",
        "$VEL.CP = 0.040",
        "LIN {X 0, Y 0, Z 2, A 90, B 0, C 180, E1 0, E2 0, E3 0, E4 0, E5 0, E6 0 } C_VEL",
        "LIN {X 10, Y 0, Z 2, A 90, B 0, C 180, E1 0, E2 0, E3 0, E4 0, E5 0, E6 0 } C_VEL",
        "LIN {X 10, Y 10, Z 2, A 90, B 0, C 180, E1 0, E2 0, E3 0, E4 0, E5 0, E6 0 } C_VEL",
        "LIN {X 0, Y 0, Z 4, A 90, B 0, C 180, E1 0, E2 0, E3 0, E4 0, E5 0, E6 0 } C_VEL",
        "LIN {X 10, Y 0, Z 4, A 90, B 0, C 180, E1 0, E2 0, E3 0, E4 0, E5 0, E6 0 } C_VEL",
        "LIN {X 10, Y 10, Z 4, A 90, B 0, C 180, E1 0, E2 0, E3 0, E4 0, E5 0, E6 0 } C_VEL",
        "END",
    };
    SceneObject part = parseSrc("Part", fullAxisLines);
    int sourceId = scene.addObject(std::move(part)).id;

    MirrorInterleaveOptions options;
    options.copies = 3;
    options.gapMm = 200.0;
    options.detourMarginMm = 100.0;

    auto merged = mirrorAndInterleave(scene, sourceId, options);
    check(merged.has_value(), "InterleaveAxisSet: 3-copy mirror+interleave succeeds");
    if (!merged.has_value()) return;

    ExportResult result;
    std::vector<std::string> exported = buildExportedLines(*merged, result);
    check(result.success, "InterleaveAxisSet: merged program exports");

    int syntheticLines = 0, incompleteLines = 0;
    for (const auto& line : exported) {
        if (line.find("GCODEFORGE INTERLEAVE TRAVEL") == std::string::npos &&
            line.find("GCODEFORGE in-layer reposition") == std::string::npos) continue;
        ++syntheticLines;
        for (const char* field : {"A ", "B ", "C ", "E1 ", "E2 ", "E3 ", "E4 ", "E5 ", "E6 "}) {
            if (line.find(field) == std::string::npos) { ++incompleteLines; break; }
        }
    }
    check(syntheticLines > 0, "InterleaveAxisSet: the merge actually produced synthetic travel/reposition lines");
    check(incompleteLines == 0,
          "InterleaveAxisSet: every synthetic line carries the full A/B/C/E1-E6 axis set (the reported bug)");
}

// Reported from real use, with photos: a real Eidos file mirrored 5 times
// and interleaved would not load on the robot at all. Root cause: the
// merged program discarded EVERYTHING outside the print/travel body --
// &ACCESS, safety interrupt declarations, BAS(#INITMOV,0), the safe-pose
// PTP, and at the end the retreat travel + $OUT[...]=FALSE shutdown block
// + $TIMER_STOP + END -- replacing it all with a bare "DEF .../END". A
// program missing &ACCESS and its safety interrupts is exactly what a
// structural validator (and, it turns out, the robot itself) rejects.
// This fixture mimics that real shape closely enough to catch it: a
// safety-looking header, a joint-space safe-pose PTP (which the parser
// tracks separately as object.startPoint, not as a Path -- see
// SrcParser.cpp), a short print body, and a shutdown footer.
std::vector<std::string> realisticEidosShapedLines() {
    return {
        "&ACCESS RVP",
        "DEF TestPart()",
        "GLOBAL INTERRUPT DECL 3 WHEN $STOPMESS==TRUE DO IR_STOPM ( )",
        "INTERRUPT ON 3",
        "BAS (#INITMOV,0 )",
        "$OUT[7]=TRUE",
        "PTP {A1 0.000, A2 -89.990, A3 99.400, A4 0.000, A5 -9.410, A6 0.000}",
        "LIN {X 100, Y 100, Z 4, A 90, B 0, C 180, E1 0, E2 0, E3 0, E4 0, E5 0, E6 0 } C_VEL",
        "LIN {X 100, Y 100, Z 2, A 90, B 0, C 180, E1 0, E2 0, E3 0, E4 0, E5 0, E6 0 } C_VEL",
        ";travel end",
        "$VEL.CP = 0.040",
        "LIN {X 100, Y 100, Z 2, A 90, B 0, C 180, E1 0, E2 0, E3 0, E4 0, E5 0, E6 0 } C_VEL",
        "LIN {X 110, Y 100, Z 2, A 90, B 0, C 180, E1 0, E2 0, E3 0, E4 0, E5 0, E6 0 } C_VEL",
        "LIN {X 110, Y 110, Z 2, A 90, B 0, C 180, E1 0, E2 0, E3 0, E4 0, E5 0, E6 0 } C_VEL",
        "LIN {X 100, Y 100, Z 4, A 90, B 0, C 180, E1 0, E2 0, E3 0, E4 0, E5 0, E6 0 } C_VEL",
        "LIN {X 110, Y 100, Z 4, A 90, B 0, C 180, E1 0, E2 0, E3 0, E4 0, E5 0, E6 0 } C_VEL",
        "LIN {X 110, Y 110, Z 4, A 90, B 0, C 180, E1 0, E2 0, E3 0, E4 0, E5 0, E6 0 } C_VEL",
        ";travel start",
        "$VEL.CP = 0.060",
        "LIN {X 100, Y 100, Z 4, A 90, B 0, C 180, E1 0, E2 0, E3 0, E4 0, E5 0, E6 0 } C_VEL",
        "LIN {X 100, Y 100, Z 40, A 90, B 0, C 180, E1 0, E2 0, E3 0, E4 0, E5 0, E6 0 } C_VEL",
        ";AIR COMMAND",
        "$OUT[5]=FALSE",
        ";EXTRUDER MOTOR COMMAND",
        "$OUT[7]=FALSE",
        ";HITT TURNING BED HEAT OFF",
        "$OUT[6]=FALSE",
        "$TIMER_STOP[ 7 ] = TRUE",
        "END",
    };
}

void testInterleavePreservesHeaderAndFooter() {
    Scene scene;
    SceneObject part = parseSrc("TestPart", realisticEidosShapedLines());
    check(part.startPoint.present, "InterleaveHeaderFooter: precondition -- the source has a joint-space safe point");
    int sourceId = scene.addObject(std::move(part)).id;

    MirrorInterleaveOptions options;
    options.copies = 5;
    options.gapMm = 200.0;
    options.detourMarginMm = 100.0;

    auto merged = mirrorAndInterleave(scene, sourceId, options);
    check(merged.has_value(), "InterleaveHeaderFooter: 5-copy mirror+interleave succeeds");
    if (!merged.has_value()) return;

    // The safe-pose joint move must survive -- it's not a Path (no X/Y/Z),
    // so it can only come through via object.startPoint, which the merge
    // used to leave untouched (present=false).
    check(merged->startPoint.present,
          "InterleaveHeaderFooter: the merged object keeps the first copy's safe-pose start point");

    ExportResult result;
    std::vector<std::string> exported = buildExportedLines(*merged, result);
    check(result.success, "InterleaveHeaderFooter: merged program exports");

    auto containsLine = [&](const std::string& needle) {
        for (const auto& line : exported) if (line.find(needle) != std::string::npos) return true;
        return false;
    };
    check(containsLine("&ACCESS"), "InterleaveHeaderFooter: exported program still has &ACCESS");
    check(containsLine("INTERRUPT ON 3"), "InterleaveHeaderFooter: exported program keeps the safety interrupt declaration");
    check(containsLine("PTP {A1"), "InterleaveHeaderFooter: exported program keeps the safe-pose PTP line");
    check(containsLine("$OUT[7]=FALSE"), "InterleaveHeaderFooter: exported program still turns the extruder off at the end");
    check(containsLine("$OUT[6]=FALSE"), "InterleaveHeaderFooter: exported program still turns the bed heat off at the end");
    check(containsLine("$OUT[5]=FALSE"), "InterleaveHeaderFooter: exported program still turns cooling/air off at the end");
    check(!exported.empty() && exported.back() == "END",
          "InterleaveHeaderFooter: exported program still ends with a real END");

    // The retreat travel's actual MOTION must survive too, not just the
    // shutdown text around it -- an earlier version of pathSrcLineSpan()
    // drew the header/footer boundary using ANY path type, which put it
    // AFTER the retreat travel (itself a tracked Travel-type path) and
    // silently excluded it: the exported program would jump straight
    // from the last print position to $OUT[...]=FALSE with no travel
    // away from the part first. The fixture's retreat lifts to Z 40
    // (distinctly higher than the Z 2/Z 4 print layers) before the
    // shutdown outputs -- check it's actually there, and before them.
    size_t liftIndex = exported.size(), shutdownIndex = exported.size();
    for (size_t i = 0; i < exported.size(); ++i) {
        if (liftIndex == exported.size() && exported[i].find("Z 40") != std::string::npos) liftIndex = i;
        if (shutdownIndex == exported.size() && exported[i].find("$OUT[5]=FALSE") != std::string::npos) shutdownIndex = i;
    }
    check(liftIndex < exported.size(),
          "InterleaveHeaderFooter: the retreat travel's own lift motion (Z 40) survives, not just the shutdown text");
    check(liftIndex < shutdownIndex,
          "InterleaveHeaderFooter: the retreat travel happens BEFORE the shutdown outputs (moves away before shutting off)");

    // The header/footer travels are real, first-class paths now -- not
    // just inert background text -- matching what the user actually asked
    // for: "keep the starting travels and safe position point for the
    // first object" and "keep the ending travels for the last mirrored
    // object." Re-parsing the export should find exactly the same total
    // path count the model itself reports.
    SceneObject reparsed = parseSrc("reparsed", exported);
    check(reparsed.paths.size() == merged->paths.size(),
          "InterleaveHeaderFooter: re-parsed export has the same path count as the merged model "
          "(header/footer travels are tracked paths, not just text)");
    check(reparsed.startPoint.present,
          "InterleaveHeaderFooter: re-parsed export still has a safe-pose start point");
}

// Forward-looking real-use question: "next I want to import sliced
// objects and they will not have start and end, and you have to fix it
// with a button or a check." A plain sliced .gcode import (via
// parser/GcodeParser.h) has none of the header/footer fix above could
// lift -- it's just G0/G1 motion lines, no &ACCESS, no DEF, no shutdown.
// Checks the "check" (objectHasBoilerplate) correctly flags such an
// object, and the "fix" (captureCellTemplate + applyCellTemplate)
// correctly wraps it using a template captured from a real-shaped file,
// anchored to the sliced object's OWN print start/end -- not the
// template's original position.
// Shaped after the real spline.dxf the user provided (3ds Max donut
// export): a $INSUNITS=5 (centimeter) header, a handful of small CLOSED
// 4-vertex rectangular POLYLINE rings at increasing Z (~0.3046 native
// units apart -- ×10 for cm->mm is ~3.046mm, matching the user's stated
// "each spline is in 3mm increment"), plus one OPEN POLYLINE with many
// vertices spanning the full Z range, standing in for the loft/rail
// construction curves 3ds Max's own exporter also emits and that must
// never be mistaken for a printable layer.
std::vector<std::string> dxfHeaderAndEntitiesLines() {
    std::vector<std::string> lines = {
        "0", "SECTION",
        "2", "HEADER",
        "9", "$INSUNITS",
        "70", "5",
        "0", "ENDSEC",
        "0", "SECTION",
        "2", "ENTITIES",
    };

    auto addRing = [&](double z, double halfWidth) {
        lines.insert(lines.end(), {"0", "POLYLINE", "100", "AcDb3dPolyline", "70", "1"});
        double coords[4][2] = {
            {-halfWidth, -halfWidth}, {halfWidth, -halfWidth}, {halfWidth, halfWidth}, {-halfWidth, halfWidth},
        };
        for (auto& xy : coords) {
            char zbuf[32];
            std::snprintf(zbuf, sizeof(zbuf), "%.4f", z);
            char xbuf[32];
            std::snprintf(xbuf, sizeof(xbuf), "%.4f", xy[0]);
            char ybuf[32];
            std::snprintf(ybuf, sizeof(ybuf), "%.4f", xy[1]);
            lines.insert(lines.end(), {"0", "VERTEX", "10", xbuf, "20", ybuf, "30", zbuf});
        }
        lines.insert(lines.end(), {"0", "SEQEND"});
    };

    addRing(0.0000, 1.0);
    addRing(0.3046, 1.1);
    addRing(0.6092, 1.2);
    addRing(0.9138, 1.1);

    // The non-printable construction curve: OPEN (group 70 has bit 0
    // clear), spans the same Z range as the real rings, many vertices.
    lines.insert(lines.end(), {"0", "POLYLINE", "100", "AcDb3dPolyline", "70", "0"});
    for (int i = 0; i <= 10; ++i) {
        double z = 0.9138 * (static_cast<double>(i) / 10.0);
        char zbuf[32];
        std::snprintf(zbuf, sizeof(zbuf), "%.4f", z);
        lines.insert(lines.end(), {"0", "VERTEX", "10", "5.0", "20", "5.0", "30", zbuf});
    }
    lines.insert(lines.end(), {"0", "SEQEND"});

    lines.insert(lines.end(), {"0", "ENDSEC"});
    return lines;
}

void testDxfParserSplineLayers() {
    DxfImportOptions options;
    options.printSpeedMps = 0.04;
    options.travelSpeedMps = 0.5;
    options.toolADegrees = 180.0;
    options.toolBDegrees = 90.0;
    options.toolCDegrees = 180.0;

    SceneObject object = parseDxfSplineLayers("Donut", dxfHeaderAndEntitiesLines(), options);

    check(object.layers.size() == 4, "DxfParser: only the 4 CLOSED rings become layers (open polyline excluded)");

    // Z-ascending order, ×10 for $INSUNITS=5 (centimeters) -> millimeters.
    if (object.layers.size() == 4) {
        checkNear(object.layers[0].z, 0.0, "DxfParser: layer 1 Z is 0mm");
        checkNear(object.layers[1].z, 3.046, "DxfParser: layer 2 Z is 3.046mm (cm->mm conversion)");
        checkNear(object.layers[2].z, 6.092, "DxfParser: layer 3 Z is 6.092mm");
        checkNear(object.layers[3].z, 9.138, "DxfParser: layer 4 Z is 9.138mm (ascending order)");
    }

    // Each ring is a 4-vertex rectangle: 4 print segments to close the
    // loop (3 sides + the closing segment back to the start).
    int totalPrintPaths = 0;
    int totalTravelPaths = 0;
    for (const auto& p : object.paths) {
        if (p.type == PathType::Print) ++totalPrintPaths;
        if (p.type == PathType::Travel) ++totalTravelPaths;
    }
    check(totalPrintPaths == 16, "DxfParser: 4 rings x 4 closing segments each = 16 print paths");
    check(totalTravelPaths == 3, "DxfParser: 3 inter-layer travels connect the 4 layers");

    // The open construction polyline's vertices (all at X=5,Y=5) must
    // never appear in the printable path set.
    bool sawConstructionVertex = false;
    for (const auto& p : object.paths) {
        if (std::abs(p.to.x - 50.0) < 0.01 && std::abs(p.to.y - 50.0) < 0.01) sawConstructionVertex = true;
    }
    check(!sawConstructionVertex, "DxfParser: the open construction curve's vertices never appear in a Path");

    // Coordinate spot-check: ring 1's start vertex is (-1,-1) cm -> (-10,-10) mm,
    // and must be readable as the first path's FROM (not its TO) -- this
    // is exactly the field CellTemplate's header anchor reads as "the
    // object's real print start."
    if (!object.paths.empty()) {
        checkNear(object.paths.front().from.x, -10.0, "DxfParser: first vertex X converted cm->mm");
        checkNear(object.paths.front().from.y, -10.0, "DxfParser: first vertex Y converted cm->mm");
    }

    // Every synthesized path carries the uniform tool orientation from options.
    bool allOrientationsMatch = true;
    for (const auto& p : object.paths) {
        if (!p.a.has_value() || !p.b.has_value() || !p.c.has_value() ||
            std::abs(*p.a - options.toolADegrees) > 1e-9 || std::abs(*p.b - options.toolBDegrees) > 1e-9 ||
            std::abs(*p.c - options.toolCDegrees) > 1e-9) {
            allOrientationsMatch = false;
        }
    }
    check(allOrientationsMatch, "DxfParser: every path carries the uniform A/B/C tool orientation from options");

    // Real KRL text: $VEL.CP inserted whenever speed changes (print vs
    // travel), and every LIN line carries the full A/B/C/E1-E6 field set
    // -- the exact completeness rule ExportValidation.h enforces, and the
    // exact "speed=0" bug class fixed earlier this session for synthetic
    // objects with no file-asserted speed to inherit for free.
    int velLineCount = 0;
    bool sawFullFieldLin = false;
    for (const auto& line : object.sourceLines) {
        if (line.rfind("$VEL.CP", 0) == 0) ++velLineCount;
        if (line.find("E6 0.0") != std::string::npos && line.find("A 180.000") != std::string::npos) {
            sawFullFieldLin = true;
        }
    }
    check(velLineCount >= 2, "DxfParser: $VEL.CP is written at least once per print/travel speed change");
    check(sawFullFieldLin, "DxfParser: LIN lines carry the full A/B/C/E1-E6 field set");

    // Built from nothing -- no real &ACCESS/safety footer of its own,
    // exactly like a plain sliced .gcode import -- so the Cell Template
    // fix must still see it as needing a fix.
    check(!objectHasBoilerplate(object),
          "DxfParser: a DXF-imported object correctly still needs the Cell Template fix");
}

void testCellTemplateFixesSlicedImport() {
    // The "known-good" source to capture a template from.
    SceneObject known = parseSrc("Known", realisticEidosShapedLines());
    check(objectHasBoilerplate(known), "CellTemplate: precondition -- the known-good fixture has real boilerplate");

    auto tmpl = captureCellTemplate(known);
    check(tmpl.has_value(), "CellTemplate: capture succeeds on an object with real boilerplate");
    if (!tmpl.has_value()) return;

    // A plain sliced import, far away from the template's own part, with
    // no header/footer at all -- exactly a "slines object."
    std::vector<std::string> plainGcode = {
        "G0 X5000 Y5000 Z10",
        "G1 X5010 Y5000 Z2 F600",
        "G1 X5010 Y5010 Z2 F600",
        "G1 X5000 Y5010 Z2 F600",
        "G0 X5000 Y5000 Z4",
        "G1 X5010 Y5000 Z4 F600",
        "G1 X5010 Y5010 Z4 F600",
    };
    SceneObject sliced = parseGcode("Sliced", plainGcode);
    check(!sliced.paths.empty(), "CellTemplate: precondition -- the sliced import actually has paths");
    check(!objectHasBoilerplate(sliced),
          "CellTemplate: the check correctly flags a plain sliced import as missing boilerplate");

    glm::dvec3 slicedFirst = sliced.paths.front().from;
    glm::dvec3 slicedLast = sliced.paths.back().to;
    size_t pathsBeforeFix = sliced.paths.size();

    bool fixed = applyCellTemplate(sliced, *tmpl);
    check(fixed, "CellTemplate: applying the template to the sliced import succeeds");
    check(objectHasBoilerplate(sliced),
          "CellTemplate: the fixed object now passes the same check that flagged it");
    check(sliced.paths.size() > pathsBeforeFix,
          "CellTemplate: the fix adds real header/footer paths, not just text");
    check(sliced.startPoint.present, "CellTemplate: the fix gives the sliced object a safe-pose start point");

    // The header's approach must end (and the footer's retreat must
    // start) at the SLICED object's own actual print start/end -- not
    // wherever the template's original part happened to be -- or the fix
    // would silently teleport the robot across the bed before printing.
    checkNear(sliced.paths.front().to.x, slicedFirst.x, "CellTemplate: header approach lands at the sliced object's own print start (X)");
    checkNear(sliced.paths.front().to.y, slicedFirst.y, "CellTemplate: header approach lands at the sliced object's own print start (Y)");

    ExportResult result;
    std::vector<std::string> exported = buildExportedLines(sliced, result);
    check(result.success, "CellTemplate: the fixed object exports successfully");
    auto containsLine = [&](const std::string& needle) {
        for (const auto& line : exported) if (line.find(needle) != std::string::npos) return true;
        return false;
    };
    check(containsLine("&ACCESS"), "CellTemplate: fixed export has &ACCESS");
    check(containsLine("$OUT[7]=FALSE"), "CellTemplate: fixed export turns the extruder off at the end");
    check(!exported.empty() && exported.back() == "END", "CellTemplate: fixed export ends with a real END");

    // Symmetric check on the footer end: its retreat must start from the
    // sliced object's own actual print END, not the template's.
    bool foundFooterAnchor = false;
    for (const auto& p : sliced.paths) {
        if (glm::length(p.from - slicedLast) < 0.01) { foundFooterAnchor = true; break; }
    }
    check(foundFooterAnchor, "CellTemplate: footer retreat starts from the sliced object's own actual print end");
}

// Ported from the web Gcode Editor's validateLines() spec (relayed via
// Codex, who read the original index.html/export-verification-v481.js):
// exactly one &ACCESS/DEF/END, no motion after END, every LIN target
// needs the full X Y Z A B C E1-E6 set. That last rule is the SAME
// completeness check that would have caught, automatically, the exact
// bug reported from real use earlier this session -- a synthetic
// interleave travel line shaped like "LIN {X 0,Y 0,Z 0}" with no A/B/C/
// E1-E6 at all, rejected by the web editor's own validator with 1630
// CRITICAL issues (see docs/LOG.md). Confirms a general validator now
// exists to catch that CLASS of bug, not just this one instance of it.
void testValidateStructureCatchesKnownBugClasses() {
    std::vector<std::string> clean = {
        "&ACCESS RVP",
        "DEF Part()",
        "LIN {X 0, Y 0, Z 2, A 90, B 0, C 180, E1 0, E2 0, E3 0, E4 0, E5 0, E6 0 } C_VEL",
        "LIN {X 10, Y 0, Z 2, A 90, B 0, C 180, E1 0, E2 0, E3 0, E4 0, E5 0, E6 0 } C_VEL",
        "END",
    };
    ValidationReport cleanReport;
    validateStructure(clean, cleanReport);
    check(cleanReport.criticalCount() == 0, "ExportValidation: a clean, complete program has zero critical issues");

    // The exact shape of the shipped-and-fixed bug: a LIN target with
    // only X/Y/Z, no A/B/C/E1-E6.
    std::vector<std::string> incompleteLin = {
        "&ACCESS RVP",
        "DEF Part()",
        "LIN {X 0,Y 0,Z 0} ; GCODEFORGE INTERLEAVE TRAVEL -- cut here after printing",
        "END",
    };
    ValidationReport incompleteReport;
    validateStructure(incompleteLin, incompleteReport);
    check(incompleteReport.hasCritical(),
          "ExportValidation: a LIN target missing A/B/C/E1-E6 is CRITICAL (the exact bug class from real use)");

    std::vector<std::string> noAccess = {"DEF Part()", "END"};
    ValidationReport noAccessReport;
    validateStructure(noAccess, noAccessReport);
    check(noAccessReport.hasCritical(), "ExportValidation: a missing &ACCESS is CRITICAL");

    std::vector<std::string> doubleEnd = {"&ACCESS RVP", "DEF Part()", "END", "END"};
    ValidationReport doubleEndReport;
    validateStructure(doubleEnd, doubleEndReport);
    check(doubleEndReport.hasCritical(), "ExportValidation: more than one END is CRITICAL");

    std::vector<std::string> motionAfterEnd = {
        "&ACCESS RVP", "DEF Part()", "END",
        "LIN {X 0, Y 0, Z 2, A 90, B 0, C 180, E1 0, E2 0, E3 0, E4 0, E5 0, E6 0 }",
    };
    ValidationReport motionAfterEndReport;
    validateStructure(motionAfterEnd, motionAfterEndReport);
    check(motionAfterEndReport.hasCritical(), "ExportValidation: a motion command after END is CRITICAL");

    // The web editor's specific carve-out: a real Eidos file may legally
    // begin already inside travel (first marker is ";travel end", no
    // preceding ";travel start") and may legally END while still in
    // travel state (the real shutdown sequence does this) -- neither is
    // an issue by itself.
    std::vector<std::string> implicitInitialTravel = {
        "&ACCESS RVP", "DEF Part()",
        ";travel end",
        "LIN {X 0, Y 0, Z 2, A 90, B 0, C 180, E1 0, E2 0, E3 0, E4 0, E5 0, E6 0 }",
        ";travel start",
        "END",
    };
    ValidationReport implicitReport;
    validateStructure(implicitInitialTravel, implicitReport);
    check(implicitReport.issues.empty(),
          "ExportValidation: implicit initial travel and ending-in-travel-state are both fine, not flagged");

    // A GENUINELY repeated unmatched ";travel end" is worth a warning
    // (not critical -- the file may still be structurally exportable).
    std::vector<std::string> repeatedUnmatchedEnd = {
        "&ACCESS RVP", "DEF Part()",
        ";travel end", ";travel end",
        "LIN {X 0, Y 0, Z 2, A 90, B 0, C 180, E1 0, E2 0, E3 0, E4 0, E5 0, E6 0 }",
        "END",
    };
    ValidationReport repeatedReport;
    validateStructure(repeatedUnmatchedEnd, repeatedReport);
    check(!repeatedReport.hasCritical(), "ExportValidation: a repeated unmatched travel end is NOT critical");
    check(repeatedReport.warningCount() > 0, "ExportValidation: a repeated unmatched travel end IS a warning");
}

// The reparse-based speed verification: compiled text is parsed FRESH
// and compared against what the object actually intended, rather than
// trusting internal bookkeeping -- this is what would have caught the
// "exported speed is 0" bug directly, instead of it needing to be
// diagnosed by hand from a bug report.
void testVerifyCompiledSpeedsCatchesMismatch() {
    SceneObject object = parseSrc("Part", sampleSrcLinesForExport());
    ExportResult result;
    std::vector<std::string> compiled = buildExportedLines(object, result);
    check(result.success, "ExportValidation: precondition -- the object exports successfully");

    ValidationReport cleanReport;
    verifyCompiledSpeeds(object, compiled, cleanReport);
    check(cleanReport.issues.empty(),
          "ExportValidation: a correctly exported program has no speed-verification issues");

    // Deliberately corrupt one path's compiled speed (simulating the
    // exact reported bug: a real motion command whose $VEL.CP silently
    // doesn't match what the object intended).
    std::vector<std::string> corrupted = compiled;
    bool foundVelCp = false;
    for (auto& line : corrupted) {
        if (line.find("$VEL.CP") != std::string::npos) {
            line = "$VEL.CP = 0.000000";
            foundVelCp = true;
            break;
        }
    }
    check(foundVelCp, "ExportValidation: precondition -- the compiled program has a $VEL.CP line to corrupt");
    ValidationReport corruptedReport;
    verifyCompiledSpeeds(object, corrupted, corruptedReport);
    check(!corruptedReport.issues.empty(),
          "ExportValidation: a corrupted speed value IS caught by reparsing the compiled text");
    check(!corruptedReport.hasCritical(),
          "ExportValidation: a speed-VALUE mismatch is a WARNING, not critical (matches export-verification-v481's downgrade)");

    // A path-count mismatch (something dropped or duplicated a motion)
    // IS structural, and stays critical -- this is the one case
    // export-verification-v481.js explicitly does NOT downgrade. Erase an
    // actual MOTION line (not a comment/speed line, which would just
    // shift a speed assertion around without changing the path count).
    std::vector<std::string> truncated = compiled;
    auto motionIt = std::find_if(truncated.begin(), truncated.end(),
        [](const std::string& l) { return l.find("LIN {X 1020") != std::string::npos; });
    check(motionIt != truncated.end(), "ExportValidation: precondition -- found a motion line to remove");
    if (motionIt != truncated.end()) truncated.erase(motionIt);
    ValidationReport truncatedReport;
    verifyCompiledSpeeds(object, truncated, truncatedReport);
    check(truncatedReport.hasCritical(),
          "ExportValidation: a PATH COUNT mismatch (structural) stays CRITICAL, never downgraded");
}

// Print-time animation: buildAnimationSequence() (subdivision + timing)
// and stateAtTime() (the shared play/scrub primitive). A 100mm straight
// print path at exactly 0.1 m/s (100mm/s) takes exactly 1 second -- easy
// round numbers make every check exact rather than approximate.
void testAnimationSequenceSubdivisionAndTiming() {
    std::vector<std::string> lines = {
        "DEF Part()",
        "$VEL.CP = 0.100",
        "LIN {X 0, Y 0, Z 2, A 90, B 0, C 180, E1 0, E2 0, E3 0, E4 0, E5 0, E6 0 } C_VEL",
        "LIN {X 100, Y 0, Z 2, A 90, B 0, C 180, E1 0, E2 0, E3 0, E4 0, E5 0, E6 0 } C_VEL",
        "END",
    };
    SceneObject object = parseSrc("Part", lines);

    AnimationSequence seq = buildAnimationSequence(object, 5.0, 0.04, true, true);
    checkNear(seq.totalDistanceMm, 100.0, "PrintAnimation: total distance matches the single 100mm path");
    checkNear(seq.totalTimeSeconds, 1.0, "PrintAnimation: 100mm at 100mm/s takes exactly 1 second");
    check(seq.segments.size() == 20,
          "PrintAnimation: a 100mm path split at a 5mm limit produces exactly 20 sub-segments");
    for (const auto& seg : seq.segments) {
        checkNear(seg.lengthMm, 5.0, "PrintAnimation: every sub-segment is exactly 5mm (equal subdivision, not leftover chunks)");
    }

    // A path shorter than the limit stays whole -- one segment, not split.
    AnimationSequence wholeSeq = buildAnimationSequence(object, 500.0, 0.04, true, true);
    check(wholeSeq.segments.size() == 1, "PrintAnimation: a path shorter than the split limit stays a single segment");
}

void testAnimationFallbackSpeedAndVisibilityFilter() {
    std::vector<std::string> lines = {
        "DEF Part()",
        ";travel start",
        "LIN {X 0, Y 0, Z 10, A 90, B 0, C 180, E1 0, E2 0, E3 0, E4 0, E5 0, E6 0 }",
        "LIN {X 50, Y 0, Z 10, A 90, B 0, C 180, E1 0, E2 0, E3 0, E4 0, E5 0, E6 0 }", // travel, NO speed at all
        ";travel end",
        "$VEL.CP = 0.200",
        "LIN {X 60, Y 0, Z 2, A 90, B 0, C 180, E1 0, E2 0, E3 0, E4 0, E5 0, E6 0 } C_VEL",
        "END",
    };
    SceneObject object = parseSrc("Part", lines);

    // A path with no $VEL.CP ever seen gets speed=0 from the parser --
    // effectiveSpeed() returns 0, which must fall back to the configured
    // fallback speed rather than producing an infinite (stalled) time.
    AnimationSequence both = buildAnimationSequence(object, 500.0, 0.04, true, true);
    check(std::isfinite(both.totalTimeSeconds) && both.totalTimeSeconds > 0.0,
          "PrintAnimation: a path with no recorded speed uses the fallback instead of stalling (infinite time)");

    AnimationSequence printOnly = buildAnimationSequence(object, 500.0, 0.04, true, false);
    for (const auto& seg : printOnly.segments) {
        check(seg.type == PathType::Print, "PrintAnimation: includeTravel=false excludes every travel segment");
    }
    check(!printOnly.segments.empty(), "PrintAnimation: includeTravel=false still keeps the print segment");

    AnimationSequence travelOnly = buildAnimationSequence(object, 500.0, 0.04, false, true);
    for (const auto& seg : travelOnly.segments) {
        check(seg.type == PathType::Travel, "PrintAnimation: includePrint=false excludes every print segment");
    }
}

void testAnimationStateAtTimeSharedByPlayAndScrub() {
    std::vector<std::string> lines = {
        "DEF Part()",
        "$VEL.CP = 0.100",
        "LIN {X 0, Y 0, Z 2, A 90, B 0, C 180, E1 0, E2 0, E3 0, E4 0, E5 0, E6 0 } C_VEL",
        "LIN {X 100, Y 0, Z 2, A 90, B 0, C 180, E1 0, E2 0, E3 0, E4 0, E5 0, E6 0 } C_VEL",
        "END",
    };
    SceneObject object = parseSrc("Part", lines);
    AnimationSequence seq = buildAnimationSequence(object, 10.0, 0.04, true, true);

    PlaybackState start = stateAtTime(seq, 0.0);
    check(!start.finished, "PrintAnimation: time 0 is not finished");
    checkNear(start.headPosition.x, 0.0, "PrintAnimation: at time 0 the head is at the path's start (X)");
    checkNear(start.progress, 0.0, "PrintAnimation: progress at time 0 is 0");

    PlaybackState half = stateAtTime(seq, 0.5); // half of 1.0 total seconds
    checkNear(half.headPosition.x, 50.0, "PrintAnimation: at half the total time, the head is halfway along X");
    checkNear(half.progress, 0.5, "PrintAnimation: progress at half time is 0.5");

    PlaybackState end = stateAtTime(seq, 1.0);
    check(end.finished, "PrintAnimation: time == totalTimeSeconds is finished");
    checkNear(end.headPosition.x, 100.0, "PrintAnimation: at the total time, the head is at the path's end (X)");

    // Scrubbing PAST the end or BEFORE the start must clamp, not misbehave.
    PlaybackState overshoot = stateAtTime(seq, 999.0);
    check(overshoot.finished, "PrintAnimation: scrubbing past the end clamps to finished");
    checkNear(overshoot.headPosition.x, 100.0, "PrintAnimation: scrubbing past the end clamps the head to the actual end");
    PlaybackState undershoot = stateAtTime(seq, -5.0);
    checkNear(undershoot.headPosition.x, 0.0, "PrintAnimation: scrubbing before the start clamps the head to the actual start");

    check(start.headDirection.x > 0.99, "PrintAnimation: head direction points along the path (+X)");

    // Same time value from two independent calls (simulating a play-step
    // and a scrub landing on the same instant) must agree exactly --
    // that's the whole point of sharing one function for both.
    PlaybackState playStep = stateAtTime(seq, 0.37);
    PlaybackState scrubJump = stateAtTime(seq, 0.37);
    checkNear(playStep.headPosition.x, scrubJump.headPosition.x, "PrintAnimation: play and scrub agree exactly at the same time value");
}

void testAnimationEmptySequence() {
    SceneObject empty;
    AnimationSequence seq = buildAnimationSequence(empty, 5.0, 0.04, true, true);
    check(seq.segments.empty(), "PrintAnimation: an object with no paths produces an empty sequence");
    PlaybackState state = stateAtTime(seq, 0.0);
    check(state.finished, "PrintAnimation: stateAtTime on an empty sequence reports finished, not a crash");
}

void testMirrorAndInterleave() {
    Scene scene;
    SceneObject part = parseSrc("Part", sampleSrcLinesForExport());
    // Cooling ON at layer 1 -- exactly the setup that vanished before.
    LayerAction cooling;
    cooling.layer = 1;
    cooling.label = "Part cooling ON";
    cooling.krlText = "$OUT[5]=TRUE";
    part.layerActions.push_back(cooling);
    int sourceId = scene.addObject(std::move(part)).id;

    MirrorInterleaveOptions options;
    options.copies = 3;
    options.gapMm = 200.0;
    options.detourMarginMm = 100.0;

    size_t objectsBefore = scene.objects.size();
    auto merged = mirrorAndInterleave(scene, sourceId, options);
    check(merged.has_value(), "MirrorInterleave: one-step mirror+link succeeds");
    if (!merged.has_value()) return;

    check(scene.objects.size() == objectsBefore + 2,
          "MirrorInterleave: 3 copies means 2 new mirrored objects added to the scene");
    check(!scene.objectLinks.empty(), "MirrorInterleave: copies are chained with links");

    // The whole point: layer actions must survive into the merged program.
    check(!merged->layerActions.empty(),
          "MirrorInterleave: layer actions carry through (cooling is NOT silently dropped)");
    check(merged->layerActions.size() == 3,
          "MirrorInterleave: each of the 3 parts keeps its own cooling action");
    bool coolingTextIntact = true;
    for (const auto& a : merged->layerActions) {
        if (a.krlText != "$OUT[5]=TRUE") coolingTextIntact = false;
    }
    check(coolingTextIntact, "MirrorInterleave: carried actions keep their exact KRL text");

    // Remapped, not left pointing at the original layer numbers.
    bool layersValid = true;
    for (const auto& a : merged->layerActions) {
        if (a.layer < 1) layersValid = false;
    }
    check(layersValid, "MirrorInterleave: carried actions have valid remapped layer numbers");

    // And the merged program must still export.
    ExportResult result;
    std::vector<std::string> exported = buildExportedLines(*merged, result);
    check(result.success, "MirrorInterleave: merged program exports");
    check(result.insertedLayerActions == 3, "MirrorInterleave: all 3 cooling actions reach the exported file");
    std::string joined;
    for (const auto& line : exported) joined += line + "\n";
    check(joined.find("$OUT[5]=TRUE") != std::string::npos,
          "MirrorInterleave: the real cooling command is present in the exported program");
}

// The whole point of a project file: everything an .src CANNOT hold must
// survive a save/load cycle. Each assertion below is something that is
// simply absent from a robot program and would be lost forever otherwise.
void testProjectRoundTrip() {
    ProjectData original;

    SceneObject part = parseSrc("Part", sampleSrcLinesForExport());
    part.transform.x = 123.5;
    part.transform.rotZDegrees = 30.0;
    part.transform.flipX = true;
    part.color = glm::vec3(0.25f, 0.5f, 0.75f);
    part.visible = false;
    part.selectedPaths.insert(2);
    part.selectedPaths.insert(4);
    part.paths[3].speedOverride = 0.0125;

    SelectionGroup group;
    group.id = "grp-1";
    group.name = "Perimeters";
    group.color = glm::vec3(0.9f, 0.1f, 0.4f);
    group.pathNumbers = {1, 2, 3};
    part.selectionGroups.push_back(group);

    LayerAction action;
    action.layer = 1;
    action.label = "Part cooling ON";
    action.krlText = "$OUT[5]=TRUE";
    part.layerActions.push_back(action);

    int idA = original.scene.addObject(std::move(part)).id;
    SceneObject second = parseSrc("Second", sampleSrcLinesForExport());
    int idB = original.scene.addObject(std::move(second)).id;
    original.scene.toggleLink(idA, idB);
    original.scene.activeObjectId = idB;

    original.bed.widthMm = 1500.0f;
    original.bed.safePointMeasured = true;
    original.bed.safePointXMm = 970.7f;
    original.bed.safePointYMm = 1760.8f;
    original.bed.safePointZMm = 1005.0f;
    original.heightmap.resize(4, 3);
    original.heightmap.at(1, 1) = 3.25f;
    original.colorMode = ColorMode::Sequence;
    original.render.mode = RenderMode::Geometry;
    original.render.showTravels = false;
    original.lighting.lights.clear();
    original.lighting.lights.push_back(Light{glm::vec3(1, 0, 0), glm::vec3(0.5f, 0.6f, 0.7f), false});
    original.lighting.lights.push_back(Light{glm::vec3(0, 1, 0), glm::vec3(1, 1, 1), true});

    const std::string path = "project_test_tmp.gfproj";
    check(saveProject(path, original), "Project: save succeeds");

    ProjectData loaded;
    check(loadProject(path, loaded), "Project: load succeeds");

    check(loaded.scene.objects.size() == 2, "Project: object count round-trips");
    if (loaded.scene.objects.size() != 2) { std::remove(path.c_str()); return; }

    const SceneObject& a = loaded.scene.objects[0];
    check(a.name == "Part", "Project: object name round-trips");
    check(!a.visible, "Project: per-object visibility round-trips");
    checkNear(a.transform.x, 123.5, "Project: transform translation round-trips");
    checkNear(a.transform.rotZDegrees, 30.0, "Project: transform rotation round-trips");
    check(a.transform.flipX, "Project: transform flip round-trips");
    check(a.paths.size() == 7, "Project: path count round-trips");
    check(a.sourceLines.size() == sampleSrcLinesForExport().size(),
          "Project: source lines round-trip (export fidelity depends on these)");

    check(a.selectedPaths.count(2) && a.selectedPaths.count(4),
          "Project: SELECTION round-trips (a .src has nowhere to store this)");
    check(a.selectionGroups.size() == 1, "Project: selection groups round-trip");
    if (!a.selectionGroups.empty()) {
        check(a.selectionGroups[0].name == "Perimeters", "Project: group name round-trips");
        check(a.selectionGroups[0].pathNumbers.size() == 3, "Project: group membership round-trips");
    }
    check(a.layerActions.size() == 1, "Project: layer actions round-trip");
    if (!a.layerActions.empty()) {
        check(a.layerActions[0].krlText == "$OUT[5]=TRUE", "Project: layer action KRL text round-trips exactly");
    }

    bool overrideKept = false;
    for (const auto& p : a.paths) {
        if (p.speedOverride.has_value() && std::abs(*p.speedOverride - 0.0125) < 1e-9) overrideKept = true;
    }
    check(overrideKept, "Project: per-path speed override round-trips");

    check(!loaded.scene.objectLinks.empty(), "Project: object links round-trip");
    check(loaded.scene.activeObjectId == idB, "Project: active object round-trips");

    checkNear(loaded.bed.widthMm, 1500.0, "Project: bed size round-trips");
    check(loaded.bed.safePointMeasured, "Project: measured safe point flag round-trips");
    checkNear(loaded.bed.safePointZMm, original.bed.safePointZMm, "Project: safe point Z round-trips");
    check(loaded.heightmap.cols == 4 && loaded.heightmap.rows == 3, "Project: heightmap size round-trips");
    checkNear(loaded.heightmap.at(1, 1), 3.25, "Project: heightmap values round-trip");

    check(loaded.colorMode == ColorMode::Sequence, "Project: color mode round-trips");
    check(loaded.render.mode == RenderMode::Geometry, "Project: render mode round-trips");
    check(!loaded.render.showTravels, "Project: display filters round-trip");

    check(loaded.lighting.lights.size() == 2,
          "Project: lights round-trip WITHOUT the default one stacking on top");
    if (loaded.lighting.lights.size() == 2) {
        check(!loaded.lighting.lights[0].enabled, "Project: per-light enabled flag round-trips");
    }

    std::remove(path.c_str());

    // A garbage file must be refused outright, leaving the caller's
    // session untouched rather than half-replaced.
    ProjectData untouched;
    untouched.scene.activeObjectId = 42;
    check(!loadProject("this_project_does_not_exist.gfproj", untouched), "Project: missing file returns false");
    check(untouched.scene.activeObjectId == 42, "Project: a failed load leaves the existing session untouched");
}

// Moving a path DIRECTLY (gizmo drag, connected drag, bed conform) must
// export, even when the object's transform is identity. Reported from
// real use: after mirroring and nudging paths closer together, the
// exported file still had the original positions.
void testDirectPathEditExports() {
    std::vector<std::string> lines = sampleSrcLinesForExport();
    SceneObject object = parseSrc("Part", lines);
    // Deliberately NO transform change -- transform stays identity, which
    // is the exact case the old "did the transform move it?" check missed.
    check(object.transform.x == 0.0, "DirectEdit: precondition -- transform is identity");

    Path* target = object.findPath(4);
    check(target != nullptr, "DirectEdit: found the path to move");
    if (!target) return;
    double originalX = target->to.x;
    target->to.x = originalX + 250.0; // as a gizmo drag would
    target->from.x += 250.0;

    ExportResult result;
    std::vector<std::string> exported = buildExportedLines(object, result);
    check(result.success, "DirectEdit: export succeeds");
    check(result.patchedCoordinateLines > 0,
          "DirectEdit: a directly-moved path patches its coordinate line (identity transform)");

    SceneObject reparsed = parseSrc("Reparsed", exported);
    const Path* reparsedTarget = reparsed.findPath(4);
    check(reparsedTarget != nullptr, "DirectEdit: re-parsed export still has the path");
    if (reparsedTarget) {
        checkNear(reparsedTarget->to.x, originalX + 250.0,
                  "DirectEdit: the MOVED position is what actually reaches the exported file");
    }
}

// Speed color: continuous gradient (red=slow, green AT the 0.6 pivot,
// blue=fast), NOT a discrete palette lookup. Verifies the pivot lands
// exactly on green regardless of what else is in the data, and that the
// two edge cases (every speed below/above the pivot) don't divide by a
// zero-width range.
void testSpeedColorGradient() {
    SceneObject object;
    Path slow; slow.number = 1; slow.type = PathType::Print; slow.motion = "LIN"; slow.speed = 0.2;
    Path pivot; pivot.number = 2; pivot.type = PathType::Print; pivot.motion = "LIN"; pivot.speed = 0.6;
    Path fast; fast.number = 3; fast.type = PathType::Print; fast.motion = "LIN"; fast.speed = 1.0;
    object.paths = {slow, pivot, fast};

    SpeedColorTable table;
    std::vector<SceneObject> objects = {object};
    table.rebuild(objects);

    glm::vec3 atPivot = table.colorFor(0.6);
    checkNear(atPivot.r, 0.20, "SpeedColor: at the 0.6 pivot, red channel is green's red (low)");
    checkNear(atPivot.g, 0.85, "SpeedColor: at the 0.6 pivot, green channel is green's green (high)");
    checkNear(atPivot.b, 0.30, "SpeedColor: at the 0.6 pivot, blue channel is green's blue (low)");

    glm::vec3 slowest = table.colorFor(0.2);
    check(slowest.r > slowest.g && slowest.r > slowest.b, "SpeedColor: the slowest speed reads red-dominant");

    glm::vec3 fastest = table.colorFor(1.0);
    check(fastest.b > fastest.r && fastest.b > fastest.g, "SpeedColor: the fastest speed reads blue-dominant");

    // Every speed on one side of the pivot must not divide by zero.
    SceneObject allFast;
    Path f1; f1.number = 1; f1.type = PathType::Print; f1.motion = "LIN"; f1.speed = 0.8;
    Path f2; f2.number = 2; f2.type = PathType::Print; f2.motion = "LIN"; f2.speed = 1.2;
    allFast.paths = {f1, f2};
    SpeedColorTable table2;
    std::vector<SceneObject> objects2 = {allFast};
    table2.rebuild(objects2);
    glm::vec3 result = table2.colorFor(0.8);
    check(std::isfinite(result.r) && std::isfinite(result.g) && std::isfinite(result.b),
          "SpeedColor: a file with every speed above the pivot doesn't produce NaN/Inf");
}

// rotateSelectedPaths(): a single path from (10,0,0) to (10,0,0) -- i.e.
// its centroid IS the point (10,0,0) -- rotated 90 degrees must land the
// point at (0,10,0), matching Transform::rotZDegrees' documented
// counterclockwise-looking-down-+Z convention (same one applyTransform()
// uses), so a path rotate and an object rotate agree on which way
// "positive" spins.
void testRotateSelectedPaths() {
    // TWO selected paths, so the centroid (0,0,5) is a real point distinct
    // from either endpoint being checked -- a single path with from==to
    // makes the point its own centroid, which trivially can't move under
    // any rotation (an earlier version of this test made exactly that
    // mistake and "failed" on correct code).
    SceneObject object;
    Path p1;
    p1.number = 1;
    p1.type = PathType::Print;
    p1.motion = "LIN";
    p1.from = glm::dvec3(-10.0, 0.0, 5.0);
    p1.to = glm::dvec3(-10.0, 0.0, 5.0);
    Path p2;
    p2.number = 2;
    p2.type = PathType::Print;
    p2.motion = "LIN";
    p2.from = glm::dvec3(10.0, 0.0, 5.0);
    p2.to = glm::dvec3(10.0, 0.0, 5.0);
    object.paths = {p1, p2};
    object.selectedPaths.insert(1);
    object.selectedPaths.insert(2);

    rotateSelectedPaths(object, 90.0);

    // Centroid is (0,0,5). Path 2's point was 10 units to the +X of the
    // centroid; after 90 degrees it should be 10 units to the +Y of it.
    checkNear(object.paths[1].to.x, 0.0, "RotatePaths: 90-degree rotation moves (10,0) to (0,10) in X");
    checkNear(object.paths[1].to.y, 10.0, "RotatePaths: 90-degree rotation moves (10,0) to (0,10) in Y");
    checkNear(object.paths[1].to.z, 5.0, "RotatePaths: Z is untouched by a Z-axis rotation");

    // An unselected path must not move at all.
    SceneObject object2;
    Path selected; selected.number = 1; selected.type = PathType::Print; selected.motion = "LIN";
    selected.from = glm::dvec3(10.0, 0.0, 0.0); selected.to = glm::dvec3(10.0, 0.0, 0.0);
    Path untouched; untouched.number = 2; untouched.type = PathType::Print; untouched.motion = "LIN";
    untouched.from = glm::dvec3(50.0, 50.0, 0.0); untouched.to = glm::dvec3(60.0, 60.0, 0.0);
    object2.paths = {selected, untouched};
    object2.selectedPaths.insert(1);
    rotateSelectedPaths(object2, 45.0);
    checkNear(object2.paths[1].from.x, 50.0, "RotatePaths: an unselected path's FROM doesn't move");
    checkNear(object2.paths[1].to.y, 60.0, "RotatePaths: an unselected path's TO doesn't move");

    // Empty selection is a no-op, not a crash.
    SceneObject object3;
    object3.paths = {selected};
    rotateSelectedPaths(object3, 90.0);
    checkNear(object3.paths[0].to.x, 10.0, "RotatePaths: an empty selection is a no-op");
}

// The gizmo's pivot-rotation primitives: rotating a point 90 degrees
// around an arbitrary (non-origin) pivot, and the closed-form whole-
// object version agreeing with it for a pure-translation transform.
void testRotatePivotMath() {
    glm::dvec3 pivot(100.0, 100.0, 0.0);
    glm::dvec3 point(110.0, 100.0, 0.0); // 10 units to the +X of the pivot
    glm::dvec3 rotated = rotatePointAroundPivotZ(point, pivot, 90.0);
    checkNear(rotated.x, 100.0, "RotatePivot: 90 degrees around a non-origin pivot -- X lands back on the pivot");
    checkNear(rotated.y, 110.0, "RotatePivot: 90 degrees around a non-origin pivot -- Y is 10 above the pivot");

    Transform t;
    t.x = 110.0;
    t.y = 100.0;
    t.z = 0.0;
    rotateObjectAroundPivot(t, pivot, 90.0);
    checkNear(t.x, 100.0, "RotatePivot: object-transform version agrees with the point primitive (X)");
    checkNear(t.y, 110.0, "RotatePivot: object-transform version agrees with the point primitive (Y)");
    checkNear(t.rotZDegrees, 90.0, "RotatePivot: rotZDegrees accumulates the applied delta");

    // The pivot itself must never move -- that's the whole point of
    // pivoting.
    glm::dvec3 pivotRotated = rotatePointAroundPivotZ(pivot, pivot, 37.0);
    checkNear(pivotRotated.x, pivot.x, "RotatePivot: the pivot point itself is a fixed point of its own rotation (X)");
    checkNear(pivotRotated.y, pivot.y, "RotatePivot: the pivot point itself is a fixed point of its own rotation (Y)");
}

// A program with no joint-space PTP at all must not sprout a phantom one.
void testStartPointAbsent() {
    SceneObject object = parseSrc("NoStart", sampleSrcLinesForExport());
    check(!object.startPoint.present, "StartPoint: a program with no joint-space PTP reports none");
}

} // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    testSrcParser();
    testGcodeParser();
    testEditorLogic();
    testUndoStack();
    testPicking();
    testMarqueeTouch();
    testPickingBackfacing();
    testBedIO();
    testGizmoMath();
    testTransformDelta();
    testGizmoOrigin();
    testSrcExporterRoundTrip();
    testSrcExporterTransform();
    testSrcExporterSpeedOverride();
    testSrcExporterLayerAction();
    testPathSplitModel();
    testPathSplitExport();
    testObjectLinkPreview();
    testObjectLinkBake();
    testBedConformSampling();
    testBedConformApply();
    testMirrorObject();
    testInterleavePrint();
    testInterleaveUnevenLayers();
    testStartPointJointMove();
    testStartPointAbsent();
    testTravelSelectionAndSpeed();
    testSafePointRoundTrip();
    testMirrorAndInterleave();
    testMirrorAndInterleaveKeepsSpeed();
    testMirrorTransitionApproachesNearEdgeNotFarEdge();
    testAnimationSequenceSubdivisionAndTiming();
    testAnimationFallbackSpeedAndVisibilityFilter();
    testAnimationStateAtTimeSharedByPlayAndScrub();
    testAnimationEmptySequence();
    testInterleaveTravelsKeepFullAxisSet();
    testInterleavePreservesHeaderAndFooter();
    testDxfParserSplineLayers();
    testCellTemplateFixesSlicedImport();
    testValidateStructureCatchesKnownBugClasses();
    testVerifyCompiledSpeedsCatchesMismatch();
    testProjectRoundTrip();
    testDirectPathEditExports();
    testSpeedColorGradient();
    testRotateSelectedPaths();
    testRotatePivotMath();
    testConnectedDragWhole();
    testConnectedDragStart();
    testConnectedDragGap();
    testFrameBounds();

    if (g_failures == 0) {
        std::printf("\nAll checks passed.\n");
        return 0;
    }
    std::fprintf(stderr, "\n%d check(s) failed.\n", g_failures);
    return 1;
}
