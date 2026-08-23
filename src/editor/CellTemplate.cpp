#include "editor/CellTemplate.h"
#include "editor/Boilerplate.h"
#include "editor/KrlLineEdit.h"
#include "model/Transform.h"
#include "parser/SrcParser.h"

#include <optional>

namespace {

bool hasHeader(const std::pair<int, int>& span) {
    return span.first > 0;
}
bool hasFooter(const SceneObject& object, const std::pair<int, int>& span) {
    return span.second + 1 < static_cast<int>(object.sourceLines.size());
}

// The line to anchor a translation on: the LAST coordinate-bearing line
// for a header (the approach's final point, which should land exactly at
// the target's print start), the FIRST for a footer (the retreat's first
// point, which should land exactly at the target's print end).
std::optional<size_t> lastCoordLine(const std::vector<std::string>& lines) {
    for (size_t i = lines.size(); i-- > 0;) {
        if (readKrlAxisValue(lines[i], 'X')) return i;
    }
    return std::nullopt;
}
std::optional<size_t> firstCoordLine(const std::vector<std::string>& lines) {
    for (size_t i = 0; i < lines.size(); ++i) {
        if (readKrlAxisValue(lines[i], 'X')) return i;
    }
    return std::nullopt;
}

// Rigidly translates every X/Y/Z token found across `lines` by `delta`.
// Lines with no coordinates (safety declarations, comments, $OUT/$TIMER
// commands, a joint-space PTP) are untouched -- replaceKrlAxisValue()
// already leaves a line alone when the axis isn't present.
void translateLines(std::vector<std::string>& lines, const glm::dvec3& delta) {
    for (auto& line : lines) {
        if (auto x = readKrlAxisValue(line, 'X')) line = replaceKrlAxisValue(line, 'X', *x + delta.x);
        if (auto y = readKrlAxisValue(line, 'Y')) line = replaceKrlAxisValue(line, 'Y', *y + delta.y);
        if (auto z = readKrlAxisValue(line, 'Z')) line = replaceKrlAxisValue(line, 'Z', *z + delta.z);
    }
}

} // namespace

bool objectHasBoilerplate(const SceneObject& object) {
    auto span = pathSrcLineSpan(object);
    if (!span) return false;
    return hasHeader(*span) && hasFooter(object, *span);
}

std::optional<CellTemplate> captureCellTemplate(const SceneObject& object) {
    auto span = pathSrcLineSpan(object);
    if (!span) return std::nullopt;
    if (!hasHeader(*span) || !hasFooter(object, *span)) return std::nullopt;

    CellTemplate tmpl;
    tmpl.headerLines = extractBoilerplate(object, 0, span->first).lines;
    tmpl.footerLines = extractBoilerplate(object, span->second + 1,
                                           static_cast<int>(object.sourceLines.size())).lines;
    tmpl.captured = !tmpl.headerLines.empty() && !tmpl.footerLines.empty();
    return tmpl.captured ? std::make_optional(tmpl) : std::nullopt;
}

bool applyCellTemplate(SceneObject& object, const CellTemplate& tmpl) {
    if (!tmpl.captured || object.paths.empty()) return false;

    glm::dvec3 objectFirst = applyTransform(object.transform, object.paths.front().from);
    glm::dvec3 objectLast = applyTransform(object.transform, object.paths.back().to);

    std::vector<std::string> newHeader = tmpl.headerLines;
    if (auto idx = lastCoordLine(newHeader)) {
        glm::dvec3 anchor(*readKrlAxisValue(newHeader[*idx], 'X'),
                           *readKrlAxisValue(newHeader[*idx], 'Y'),
                           *readKrlAxisValue(newHeader[*idx], 'Z'));
        translateLines(newHeader, objectFirst - anchor);
    }

    std::vector<std::string> newFooter = tmpl.footerLines;
    if (auto idx = firstCoordLine(newFooter)) {
        glm::dvec3 anchor(*readKrlAxisValue(newFooter[*idx], 'X'),
                           *readKrlAxisValue(newFooter[*idx], 'Y'),
                           *readKrlAxisValue(newFooter[*idx], 'Z'));
        translateLines(newFooter, objectLast - anchor);
    }

    // Re-parse the translated (now world-space-correct-for-this-object)
    // text through the real SRC parser, rather than hand-rolling Path
    // construction -- reuses the exact same motion/travel/layer/
    // startPoint detection a real file gets, so a header with a joint
    // PTP or multiple approach steps is handled the same way either way.
    SceneObject parsedHeader = parseSrc("header", newHeader);
    SceneObject parsedFooter = parseSrc("footer", newFooter);

    // parsedHeader/parsedFooter's paths are in WORLD space (that's what
    // newHeader/newFooter's text now says) -- but object.paths is always
    // LOCAL space (object.transform applies on top at render/export
    // time), so convert back before inserting.
    int headerLineCount = static_cast<int>(newHeader.size());
    for (auto& p : object.paths) {
        if (p.srcLine >= 0) p.srcLine += headerLineCount;
    }
    for (auto& p : object.paths) {
        if (p.cloneTemplateSrcLine >= 0) p.cloneTemplateSrcLine += headerLineCount;
    }

    std::vector<Path> headerPaths;
    for (const auto& p : parsedHeader.paths) {
        Path local = p;
        local.from = inverseApplyTransform(object.transform, p.from);
        local.to = inverseApplyTransform(object.transform, p.to);
        headerPaths.push_back(local);
    }
    std::vector<Path> footerPaths;
    int footerBaseLine = headerLineCount + static_cast<int>(object.sourceLines.size());
    for (const auto& p : parsedFooter.paths) {
        Path local = p;
        local.from = inverseApplyTransform(object.transform, p.from);
        local.to = inverseApplyTransform(object.transform, p.to);
        local.srcLine = (p.srcLine >= 0) ? p.srcLine + footerBaseLine : p.srcLine;
        footerPaths.push_back(local);
    }

    object.sourceLines.insert(object.sourceLines.begin(), newHeader.begin(), newHeader.end());
    object.sourceLines.insert(object.sourceLines.end(), newFooter.begin(), newFooter.end());

    object.paths.insert(object.paths.begin(), headerPaths.begin(), headerPaths.end());
    object.paths.insert(object.paths.end(), footerPaths.begin(), footerPaths.end());

    // Renumber sequentially so path.number stays a dense 1..N index
    // (header paths now come first, footer paths last).
    int number = 1;
    for (auto& p : object.paths) p.number = number++;

    if (parsedHeader.startPoint.present) {
        object.startPoint = parsedHeader.startPoint;
        if (object.startPoint.position.has_value()) {
            object.startPoint.position = inverseApplyTransform(object.transform, *object.startPoint.position);
        }
    }

    return true;
}
