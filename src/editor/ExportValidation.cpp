#include "editor/ExportValidation.h"
#include "parser/SrcParser.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <regex>

namespace {

const std::regex kMotionRe(R"(^\s*(LIN|PTP|CIRC|SPL)\b)", std::regex::icase);
const std::regex kLinRe(R"(^\s*LIN\b)", std::regex::icase);
const std::regex kAccessRe(R"(^\s*&ACCESS\b)", std::regex::icase);
const std::regex kDefRe(R"(^\s*DEF\b)", std::regex::icase);
const std::regex kEndRe(R"(^\s*END\s*$)", std::regex::icase);

std::string toLower(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool hasField(const std::string& line, const std::string& field) {
    // \b<FIELD>\s*[-+]?\d -- same shape as KrlLineEdit's per-axis regexes,
    // generalized to any field name (A, B, C, E1..E6). Word boundary
    // before the field keeps "E1" from matching inside "E10" etc.
    std::regex re("\\b" + field + R"(\s*[-+]?\d)", std::regex::icase);
    return std::regex_search(line, re);
}

void addIssue(ValidationReport& report, ValidationSeverity severity, const std::string& message) {
    report.issues.push_back({severity, message});
}

std::string formatSpeed(double value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.6f", value);
    return buffer;
}

} // namespace

int ValidationReport::criticalCount() const {
    return static_cast<int>(std::count_if(issues.begin(), issues.end(),
        [](const ValidationIssue& i) { return i.severity == ValidationSeverity::Critical; }));
}

int ValidationReport::warningCount() const {
    return static_cast<int>(std::count_if(issues.begin(), issues.end(),
        [](const ValidationIssue& i) { return i.severity == ValidationSeverity::Warning; }));
}

namespace {

// The 6 structural checks, each self-contained (own scan over `lines`)
// so they can run individually from the UI, not just bundled together --
// a little redundant scanning across checks, but these run against a
// single robot program's worth of text (thousands of lines at most), so
// clarity wins over the micro-optimization of sharing one combined pass.

void checkSingleAccess(const SceneObject&, const std::vector<std::string>& lines, double, ValidationReport& report) {
    int count = 0;
    for (const auto& line : lines) if (std::regex_search(line, kAccessRe)) ++count;
    if (count != 1) addIssue(report, ValidationSeverity::Critical,
        "Expected exactly one &ACCESS; found " + std::to_string(count) + ".");
}

void checkSingleDef(const SceneObject&, const std::vector<std::string>& lines, double, ValidationReport& report) {
    int count = 0;
    for (const auto& line : lines) if (std::regex_search(line, kDefRe)) ++count;
    if (count != 1) addIssue(report, ValidationSeverity::Critical,
        "Expected exactly one DEF; found " + std::to_string(count) + ".");
}

void checkSingleEnd(const SceneObject&, const std::vector<std::string>& lines, double, ValidationReport& report) {
    int count = 0;
    for (const auto& line : lines) if (std::regex_search(line, kEndRe)) ++count;
    if (count != 1) addIssue(report, ValidationSeverity::Critical,
        "Expected exactly one END; found " + std::to_string(count) + ".");
}

void checkNoMotionAfterEnd(const SceneObject&, const std::vector<std::string>& lines, double, ValidationReport& report) {
    int firstEndIndex = -1;
    for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
        if (std::regex_search(lines[static_cast<size_t>(i)], kEndRe)) { firstEndIndex = i; break; }
    }
    if (firstEndIndex < 0) return;
    for (int i = firstEndIndex + 1; i < static_cast<int>(lines.size()); ++i) {
        if (std::regex_search(lines[static_cast<size_t>(i)], kMotionRe)) {
            addIssue(report, ValidationSeverity::Critical,
                "Motion command after END at line " + std::to_string(i + 1) + ".");
        }
    }
}

void checkLinAxisCompleteness(const SceneObject&, const std::vector<std::string>& lines, double, ValidationReport& report) {
    static const std::vector<std::string> kRequiredFields = {"X", "Y", "Z", "A", "B", "C",
                                                               "E1", "E2", "E3", "E4", "E5", "E6"};
    for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
        const std::string& line = lines[static_cast<size_t>(i)];
        if (!std::regex_search(line, kLinRe)) continue;
        std::vector<std::string> missing;
        for (const auto& field : kRequiredFields) {
            if (!hasField(line, field)) missing.push_back(field);
        }
        if (!missing.empty()) {
            std::string joined;
            for (size_t m = 0; m < missing.size(); ++m) {
                if (m > 0) joined += ", ";
                joined += missing[m];
            }
            addIssue(report, ValidationSeverity::Critical,
                "Incomplete LIN target at line " + std::to_string(i + 1) + ": missing " + joined + ".");
        }
    }
}

// Travel markers are STATE markers, not balanced blocks -- a real Eidos
// file may legally begin already inside a travel section (the first
// marker in the file being ";travel end" rather than ";travel start"),
// and may legally END while still in travel state (the final shutdown
// sequence). Establishing the INITIAL state via a pre-scan (same
// technique SrcParser.cpp uses to parse the file itself) makes that
// opening ";travel end" consistent with the state it establishes, rather
// than needing separate "have we used our one free pass yet" bookkeeping
// -- a REPEATED, genuinely unmatched ";travel end" after that is what's
// actually worth flagging.
void checkTravelMarkerBalance(const SceneObject&, const std::vector<std::string>& lines, double, ValidationReport& report) {
    bool travelState = false;
    for (const auto& line : lines) {
        const std::string lower = toLower(line);
        if (lower.find(";travel start") != std::string::npos) { travelState = false; break; }
        if (lower.find(";travel end") != std::string::npos) { travelState = true; break; }
    }
    for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
        const std::string lower = toLower(lines[static_cast<size_t>(i)]);
        if (lower.find(";travel start") != std::string::npos) travelState = true;
        if (lower.find(";travel end") != std::string::npos) {
            if (!travelState) {
                addIssue(report, ValidationSeverity::Warning,
                    "Travel end appears while travel state is already off at line " + std::to_string(i + 1) + ".");
            }
            travelState = false;
        }
    }
}

void checkSpeedMatch(const SceneObject& object, const std::vector<std::string>& compiledLines,
                      double speedToleranceMps, ValidationReport& report) {
    SceneObject reparsed = parseSrc("__export_verify__", compiledLines);

    if (reparsed.paths.size() != object.paths.size()) {
        addIssue(report, ValidationSeverity::Critical,
            "Exported path count (" + std::to_string(reparsed.paths.size()) +
            ") does not match the object's own path count (" + std::to_string(object.paths.size()) +
            ") -- structural export mismatch.");
        return; // can't meaningfully compare path-by-path after a count mismatch
    }

    int mismatches = 0;
    for (size_t i = 0; i < object.paths.size(); ++i) {
        const Path& intended = object.paths[i];
        if (intended.motion == "PTP") continue; // $VEL.CP doesn't control PTP motion

        const Path& compiled = reparsed.paths[i];
        double expected = intended.effectiveSpeed();
        double actual = compiled.speed.value_or(0.0);
        if (std::abs(expected - actual) <= speedToleranceMps) continue;

        ++mismatches;
        if (mismatches > 12) continue; // same cap the web editor's verifier uses
        addIssue(report, ValidationSeverity::Warning,
            "Path " + std::to_string(intended.number) + ": exported speed " + formatSpeed(actual) +
            " does not match the intended " + formatSpeed(expected) + ".");
    }
    if (mismatches > 12) {
        addIssue(report, ValidationSeverity::Warning,
            std::to_string(mismatches - 12) + " further speed mismatch(es) not shown.");
    }
}

} // namespace

void validateStructure(const std::vector<std::string>& lines, ValidationReport& report) {
    SceneObject unused; // the 6 structural checks all ignore `object`
    checkSingleAccess(unused, lines, 0.0, report);
    checkSingleDef(unused, lines, 0.0, report);
    checkSingleEnd(unused, lines, 0.0, report);
    checkNoMotionAfterEnd(unused, lines, 0.0, report);
    checkLinAxisCompleteness(unused, lines, 0.0, report);
    checkTravelMarkerBalance(unused, lines, 0.0, report);
}

void verifyCompiledSpeeds(const SceneObject& object, const std::vector<std::string>& compiledLines,
                           ValidationReport& report, double toleranceMps) {
    checkSpeedMatch(object, compiledLines, toleranceMps, report);
}

ValidationReport validateForExport(const SceneObject& object, const std::vector<std::string>& compiledLines) {
    ValidationReport report;
    validateStructure(compiledLines, report);
    verifyCompiledSpeeds(object, compiledLines, report);
    return report;
}

std::vector<NamedValidationCheck> exportValidationChecks() {
    return {
        {"Exactly one &ACCESS", checkSingleAccess},
        {"Exactly one DEF", checkSingleDef},
        {"Exactly one END", checkSingleEnd},
        {"No motion after END", checkNoMotionAfterEnd},
        {"Every LIN has full X/Y/Z/A/B/C/E1-E6", checkLinAxisCompleteness},
        {"Travel markers balanced", checkTravelMarkerBalance},
        {"Exported speeds match intended", checkSpeedMatch},
    };
}
