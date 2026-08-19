// A small hand-checked regression test for SrcParser and GcodeParser.
// No test framework -- just asserts against numbers worked out by hand by
// tracing the parser's algorithm line by line (see the comments below).
// Run via `ctest` from the build directory, or the executable directly.

#include "parser/SrcParser.h"
#include "parser/GcodeParser.h"

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

} // namespace

int main() {
    testSrcParser();
    testGcodeParser();

    if (g_failures == 0) {
        std::printf("\nAll checks passed.\n");
        return 0;
    }
    std::fprintf(stderr, "\n%d check(s) failed.\n", g_failures);
    return 1;
}
