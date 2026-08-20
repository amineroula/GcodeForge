#pragma once

#include <glm/glm.hpp>
#include <optional>
#include <string>

// One straight-line robot motion, reconstructed from a single KUKA SRC
// motion line (or a G-code move once the G-code parser exists). Deliberately
// carries more than two points -- it stays connected to the manufacturing
// information the operator actually cares about, mirroring the original's
// per-path metadata (see docs/PLAN.md and docs/EVOLUTION.md sections 1-2).
enum class PathType {
    Print,  // extrusion motion -- the robot is depositing material
    Travel  // non-print motion -- moving with the nozzle off
};

struct Path {
    int number = 0;          // 1-based index within the object, matches the original's `path` field
    glm::dvec3 from{0.0};
    glm::dvec3 to{0.0};
    PathType type = PathType::Travel;

    // Layer index for print paths (1-based). -1 for travel paths, which
    // aren't assigned a layer the same way in the original.
    int layer = -1;

    // The KUKA motion command this line used: LIN, PTP, CIRC, or SPL.
    // Speed overrides only apply to continuous-path motion ($VEL.CP
    // controls LIN/CIRC/SPL, not PTP) -- see docs/PLAN.md milestone 9.
    std::string motion = "LIN";

    // Effective $VEL.CP speed in effect at this line, in m/s, as found in
    // the source file. std::nullopt if no $VEL.CP/BAS(#VEL_CP,...) command
    // had been seen yet when this path was parsed.
    std::optional<double> speed;

    // Operator-applied override, stored separately from the parsed `speed`
    // so re-parsing the source never silently discards an edit.
    std::optional<double> speedOverride;

    // Which line of the original SRC file this motion command came from
    // (0-based index into the object's stored source lines), so edits can
    // eventually be written back to the right place.
    int srcLine = -1;

    // Set ONLY on a synthetic path created by editor::splitSelectedPaths()
    // (srcLine stays -1 for these -- there's no existing source line that
    // is this path). Points at the srcLine of the sibling half that DOES
    // own a real line, so SrcExporter can synthesize a brand-new motion
    // line: clone that line's full text (motion command, E1-E6, C_VEL,
    // trailing comment -- everything replaceAxisValue() doesn't touch)
    // and just substitute this path's own X/Y/Z, inserted right before
    // the template line. -1 (the default) means "not a synthetic path."
    int cloneTemplateSrcLine = -1;

    // Tool orientation (A/B/C, degrees) at the END of this motion, as
    // found in the source. Added specifically so SRC export can
    // reconstruct a real, safe robot pose -- exporting X/Y/Z alone and
    // dropping orientation would silently produce an incomplete/invalid
    // motion command for a real KUKA controller. std::nullopt if the
    // source line didn't specify that axis.
    std::optional<double> a;
    std::optional<double> b;
    std::optional<double> c;

    double effectiveSpeed() const {
        if (speedOverride.has_value()) return *speedOverride;
        return speed.value_or(0.0);
    }
};
