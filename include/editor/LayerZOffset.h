#pragma once

#include "model/SceneObject.h"

// A flat, layer-based Z correction -- distinct from editor/BedConform.h,
// which samples a measured elevation SPATIALLY (by XY position) and only
// affects the bottom few layers by design. This is the opposite shape:
// no spatial sampling at all, just "this layer measured 0.005mm high,
// shift it (and however much of what's printed above it) to match."
//
// Real request, verbatim: "I want this layer to have .205 in Z not .2,
// this will affect the next layers" -- with the propagation choice
// clarified as four distinct modes, not just one behavior.
enum class ZOffsetMode {
    SingleLayer,   // only startLayer gets the full delta
    CascadeAll,    // startLayer and every layer above it get the full delta
    CascadeCount,  // startLayer and the next `layerCount - 1` layers above it get the full delta, then nothing
    CascadeTaper,  // full delta at startLayer, linearly decreasing to 0 over `layerCount` layers above it
};

struct ZOffsetOptions {
    int startLayer = 1;
    double deltaZMm = 0.0;
    ZOffsetMode mode = ZOffsetMode::SingleLayer;
    // Layer count above startLayer the effect reaches -- only meaningful
    // for CascadeCount/CascadeTaper (ignored otherwise). Must be >= 1.
    int layerCount = 1;
};

// Shifts Z on every PRINT path (travel paths carry no layer number and
// are left untouched, same as editor/BedConform.h's own established
// behavior) whose layer falls within the options' scope, by the
// appropriate weight * deltaZMm. Layers below startLayer are never
// touched, regardless of mode.
void applyLayerZOffset(SceneObject& object, const ZOffsetOptions& options);
