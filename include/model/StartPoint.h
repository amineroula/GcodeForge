#pragma once

#include <glm/glm.hpp>
#include <optional>

// The "first safe position": the joint-space PTP move an Eidos-generated
// program issues before any Cartesian motion, to put the arm in one known,
// repeatable posture before it flies to the print start. In a real file
// this looks like:
//
//     PTP {A1 0.000, A2 -89.990, A3 99.400, A4 0.000, A5 -9.410, A6 0.000}
//
// Note what it does NOT have: X, Y, or Z. It commands six AXIS ANGLES, not
// a Cartesian point -- which is exactly why the parser used to drop it
// silently (its "does this line have x && y && z?" test failed), and why
// it was invisible in the viewport. A Cartesian point can be reached by
// several different arm configurations (elbow up/down, wrist flipped);
// commanding joint angles removes that ambiguity so the arm always starts
// from the same posture.
//
// Because the file states no Cartesian position for it, THERE IS NO WAY TO
// COMPUTE where this pose actually is in space without the robot's DH
// parameters, which the program doesn't carry. So `position` below is a
// display ANCHOR, not a derived truth: it defaults to the program's first
// Cartesian point (where the arm is headed next -- the most meaningful
// proxy available) and the operator can move it. Moving it is a
// display/planning aid; see editor/SrcExporter for what export does with
// it, which is deliberately conservative.
struct JointPose {
    double a1 = 0.0;
    double a2 = 0.0;
    double a3 = 0.0;
    double a4 = 0.0;
    double a5 = 0.0;
    double a6 = 0.0;
};

struct StartPoint {
    bool present = false;
    int srcLine = -1;       // the PTP line in the object's sourceLines
    JointPose joints;       // the six commanded axis angles, exactly as parsed
    bool jointSpace = true; // true when the source line used the A1-A6 form (no X/Y/Z)

    // Where to DRAW this pose, in the object's LOCAL space (same space as
    // Path::from/to, so the object's transform applies to it identically
    // and it travels with the object when moved). Defaults to the
    // program's first Cartesian point. std::nullopt until the parser has
    // seen a Cartesian point to anchor to.
    std::optional<glm::dvec3> position;

    // Set once the operator repositions it by hand, so the UI can show
    // that this is no longer the auto-derived anchor and export can tell
    // an intentional edit from an untouched default.
    bool movedByOperator = false;
};
