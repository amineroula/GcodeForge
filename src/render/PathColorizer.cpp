#include "render/PathColorizer.h"

#include <algorithm>
#include <cstdio>

namespace {

glm::vec3 hexToVec3(const char* hex) {
    unsigned int r = 0, g = 0, b = 0;
    std::sscanf(hex, "#%2x%2x%2x", &r, &g, &b);
    return glm::vec3(r / 255.0f, g / 255.0f, b / 255.0f);
}

const glm::vec3 kTravelColor = hexToVec3("#ff9f1a");
const glm::vec3 kPrintColor = hexToVec3("#28c840");
const glm::vec3 kGroupFallback = hexToVec3("#687780");
const glm::vec3 kSpeedFallback = hexToVec3("#89969e");
const glm::vec3 kSelectionHighlight = hexToVec3("#39ff5a"); // bright green, outside the normal palette on purpose

// Points to a SelectionGroup within an object that contains the given path
// number, or nullptr. Mirrors the original's groupForPath().
const SelectionGroup* groupForPath(const SceneObject& object, int pathNumber) {
    for (const auto& group : object.selectionGroups) {
        if (std::find(group.pathNumbers.begin(), group.pathNumbers.end(), pathNumber) != group.pathNumbers.end()) {
            return &group;
        }
    }
    return nullptr;
}

} // namespace

const std::vector<glm::vec3>& colorPalette() {
    static const std::vector<glm::vec3> kPalette = {
        hexToVec3("#19c37d"), hexToVec3("#f59e0b"), hexToVec3("#7c3aed"), hexToVec3("#ef4444"),
        hexToVec3("#06b6d4"), hexToVec3("#eab308"), hexToVec3("#ec4899"), hexToVec3("#22c55e"),
        hexToVec3("#3b82f6"), hexToVec3("#f97316"), hexToVec3("#8b5cf6"), hexToVec3("#14b8a6"),
        hexToVec3("#84cc16"), hexToVec3("#d946ef"), hexToVec3("#0ea5e9"), hexToVec3("#a855f7"),
        hexToVec3("#e11d48"), hexToVec3("#10b981"),
    };
    return kPalette;
}

void SpeedColorTable::rebuild(const std::vector<SceneObject>& objects) {
    std::vector<double> unique;
    for (const auto& object : objects) {
        for (const auto& path : object.paths) {
            double speed = path.effectiveSpeed();
            if (std::find(unique.begin(), unique.end(), speed) == unique.end()) {
                unique.push_back(speed);
            }
        }
    }
    std::sort(unique.begin(), unique.end());
    sortedSpeeds_ = std::move(unique);

    maxPathNumberByObject_.clear();
    for (const auto& object : objects) {
        int highest = 0;
        for (const auto& path : object.paths) highest = std::max(highest, path.number);
        maxPathNumberByObject_[object.id] = std::max(highest, 1);
    }
}

int SpeedColorTable::maxPathNumber(int objectId) const {
    auto it = maxPathNumberByObject_.find(objectId);
    return it == maxPathNumberByObject_.end() ? 1 : it->second;
}

glm::vec3 SpeedColorTable::colorFor(double speed) const {
    auto it = std::find(sortedSpeeds_.begin(), sortedSpeeds_.end(), speed);
    if (it == sortedSpeeds_.end()) return kSpeedFallback;
    size_t index = static_cast<size_t>(std::distance(sortedSpeeds_.begin(), it));
    const auto& palette = colorPalette();
    return palette[index % palette.size()];
}

glm::vec3 pathColor(const SceneObject& object, const Path& path, ColorMode mode,
                     const SpeedColorTable& speedColors) {
    switch (mode) {
        case ColorMode::Object:
            return object.color;
        case ColorMode::Type:
            return path.type == PathType::Travel ? kTravelColor : kPrintColor;
        case ColorMode::Layer: {
            const auto& palette = colorPalette();
            int layer = path.layer > 0 ? path.layer : 1;
            return palette[static_cast<size_t>(layer - 1) % palette.size()];
        }
        case ColorMode::Group: {
            const SelectionGroup* group = groupForPath(object, path.number);
            return group ? group->color : kGroupFallback;
        }
        case ColorMode::Speed:
            return speedColors.colorFor(path.effectiveSpeed());
        case ColorMode::Sequence: {
            // Blue -> cyan -> green -> yellow -> red across the program.
            // A continuous ramp, NOT the 18-color palette: the palette
            // wraps every 18 entries, which on a 24k-path file would
            // repeat constantly and destroy the ordering signal that is
            // this mode's entire purpose.
            float t = static_cast<float>(path.number - 1) /
                       static_cast<float>(std::max(speedColors.maxPathNumber(object.id) - 1, 1));
            t = std::clamp(t, 0.0f, 1.0f);
            const glm::vec3 stops[5] = {
                glm::vec3(0.15f, 0.30f, 0.95f), // first
                glm::vec3(0.10f, 0.80f, 0.85f),
                glm::vec3(0.20f, 0.85f, 0.30f),
                glm::vec3(0.95f, 0.85f, 0.15f),
                glm::vec3(0.95f, 0.20f, 0.15f), // last
            };
            float scaled = t * 4.0f;
            int lo = std::clamp(static_cast<int>(scaled), 0, 3);
            return glm::mix(stops[lo], stops[lo + 1], scaled - lo);
        }
    }
    return kSpeedFallback;
}

glm::vec3 selectionHighlightColor() {
    return kSelectionHighlight;
}
