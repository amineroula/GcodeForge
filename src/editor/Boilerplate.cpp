#include "editor/Boilerplate.h"
#include "editor/KrlLineEdit.h"
#include "model/Transform.h"

#include <algorithm>
#include <limits>
#include <map>

std::optional<std::pair<int, int>> pathSrcLineSpan(const SceneObject& object) {
    int minLine = std::numeric_limits<int>::max();
    int maxLine = -1;
    for (const auto& path : object.paths) {
        if (path.srcLine < 0 || path.type != PathType::Print) continue;
        minLine = std::min(minLine, path.srcLine);
        maxLine = std::max(maxLine, path.srcLine);
    }
    if (maxLine < 0) return std::nullopt;
    return std::make_pair(minLine, maxLine);
}

BoilerplateSlice extractBoilerplate(const SceneObject& object, int startLine, int endLineExclusive) {
    BoilerplateSlice slice;
    startLine = std::max(startLine, 0);
    endLineExclusive = std::min(endLineExclusive, static_cast<int>(object.sourceLines.size()));
    if (startLine >= endLineExclusive) return slice;

    std::map<int, const Path*> byLine;
    for (const auto& path : object.paths) {
        if (path.srcLine >= startLine && path.srcLine < endLineExclusive) byLine[path.srcLine] = &path;
    }

    for (int i = startLine; i < endLineExclusive; ++i) {
        std::string line = object.sourceLines[static_cast<size_t>(i)];
        auto it = byLine.find(i);
        if (it != byLine.end()) {
            const Path* srcPath = it->second;
            glm::dvec3 worldTo = applyTransform(object.transform, srcPath->to);
            glm::dvec3 worldFrom = applyTransform(object.transform, srcPath->from);
            line = replaceKrlAxisValue(line, 'X', worldTo.x);
            line = replaceKrlAxisValue(line, 'Y', worldTo.y);
            line = replaceKrlAxisValue(line, 'Z', worldTo.z);

            Path cloned = *srcPath;
            cloned.from = worldFrom;
            cloned.to = worldTo;
            cloned.srcLine = static_cast<int>(slice.lines.size());
            slice.paths.push_back(cloned);
        }
        slice.lines.push_back(line);
    }
    return slice;
}
