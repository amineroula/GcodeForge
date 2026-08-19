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

#include <glm/gtc/matrix_transform.hpp>
#include <cstdio>
#include <cstdlib>
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
        "LIN {X 1000,Y 500,Z 82}",
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

} // namespace

int main() {
    testSrcParser();
    testGcodeParser();
    testEditorLogic();
    testUndoStack();
    testPicking();

    if (g_failures == 0) {
        std::printf("\nAll checks passed.\n");
        return 0;
    }
    std::fprintf(stderr, "\n%d check(s) failed.\n", g_failures);
    return 1;
}
