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

    check(mirror.transform.flipX != source.transform.flipX, "MirrorObject: mirror has flipX toggled relative to the source");
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
    double worstTravelDz = 0.0;
    for (const auto& p : merged->paths) {
        if (p.type != PathType::Travel) continue;
        worstTravelDz = std::max(worstTravelDz, std::abs(p.to.z - p.from.z));
    }
    check(worstTravelDz <= layerStep + 1e-6,
          "InterleavePrint: no travel hops above the layer step (no clearance lift)");

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
    testProjectRoundTrip();
    testDirectPathEditExports();
    testSpeedColorGradient();
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
