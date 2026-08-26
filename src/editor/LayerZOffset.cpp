#include "editor/LayerZOffset.h"

#include <algorithm>

namespace {

// 1.0 = full effect, 0.0 = no effect, matching the four modes exactly.
double weightForLayer(int layer, const ZOffsetOptions& options) {
    if (layer < options.startLayer) return 0.0;

    switch (options.mode) {
        case ZOffsetMode::SingleLayer:
            return (layer == options.startLayer) ? 1.0 : 0.0;
        case ZOffsetMode::CascadeAll:
            return 1.0;
        case ZOffsetMode::CascadeCount: {
            int count = std::max(options.layerCount, 1);
            return (layer < options.startLayer + count) ? 1.0 : 0.0;
        }
        case ZOffsetMode::CascadeTaper: {
            int count = std::max(options.layerCount, 1);
            double w = 1.0 - static_cast<double>(layer - options.startLayer) / count;
            return std::clamp(w, 0.0, 1.0);
        }
    }
    return 0.0;
}

} // namespace

void applyLayerZOffset(SceneObject& object, const ZOffsetOptions& options) {
    for (auto& path : object.paths) {
        if (path.type != PathType::Print) continue;
        double weight = weightForLayer(path.layer, options);
        if (weight <= 0.0) continue;

        double shift = weight * options.deltaZMm;
        path.from.z += shift;
        path.to.z += shift;
    }
}
