#pragma once

#include "model/SceneObject.h"

#include <string>
#include <vector>

// Pre-export checks, ported from the web Gcode Editor's validateLines()
// and the reparse-based verifyCompiledObjectSpeeds() (see
// export-verification-v481.js's severity-downgrade behavior). Two kinds
// of problem, deliberately kept separate:
//
//  - CRITICAL: the exported text is structurally broken -- wrong &ACCESS/
//    DEF/END counts, motion after END, a LIN target missing an axis field
//    a real controller needs. This is the same class of bug that shipped
//    once already (a synthetic travel line missing A/B/C/E1-E6, see
//    docs/LOG.md) -- this check exists specifically so that never has to
//    be caught by hand from a bug report again.
//  - WARNING: the file is structurally sound but a speed value doesn't
//    match what was intended, or an unmatched travel marker. Worth
//    reviewing, not worth blocking on -- matches export-verification-
//    v481.js downgrading ONLY "SRC speed verification failed" to
//    non-blocking, while everything structural still blocks.
enum class ValidationSeverity { Warning, Critical };

struct ValidationIssue {
    ValidationSeverity severity;
    std::string message;
};

struct ValidationReport {
    std::vector<ValidationIssue> issues;

    int criticalCount() const;
    int warningCount() const;
    bool hasCritical() const { return criticalCount() > 0; }
};

// Structural checks on already-compiled KRL text: exactly one &ACCESS,
// exactly one DEF, exactly one standalone END, no recognized motion
// (LIN/PTP/CIRC/SPL) after that END, and every LIN target line carries
// the full X Y Z A B C E1 E2 E3 E4 E5 E6 field set -- the same
// completeness rule the web editor's structural validator enforces,
// stricter than what the path parser itself requires (it only needs
// X/Y/Z to recognize a path at all). Also flags an unmatched ";travel
// end" as a warning, except the one permitted implicit initial one (a
// real Eidos file can legally begin already inside a travel section).
void validateStructure(const std::vector<std::string>& lines, ValidationReport& report);

// Reparses `compiledLines` as a fresh object and compares it against
// `object`'s own paths, in order: a PATH COUNT mismatch is a structural
// problem (critical -- something in compilation dropped or added a
// motion). Otherwise, for every non-PTP path, compares the compiled
// text's actual effective speed against what `object` intended
// (Path::effectiveSpeed()) at a tolerance of 1e-9, reporting mismatches
// as warnings (capped at 12, matching the web editor's own cap) rather
// than blocking -- a speed-value mismatch means the exported file will
// run at the wrong (but still well-formed) speed, not that it's broken.
void verifyCompiledSpeeds(const SceneObject& object, const std::vector<std::string>& compiledLines,
                           ValidationReport& report);

// Runs every check and returns the combined report.
ValidationReport validateForExport(const SceneObject& object, const std::vector<std::string>& compiledLines);
