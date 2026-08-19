#pragma once

// A detected print layer: a contiguous run of print paths at (roughly) the
// same Z height. The original detects a new layer whenever a print move's
// Z differs from the previous print move's Z by more than a small epsilon
// -- ported exactly in SrcParser (see docs/LOG.md milestone 5).
struct Layer {
    int layer = 0;      // 1-based layer number
    double z = 0.0;
    int startPath = 0;  // path number (1-based) where this layer begins
    int endPath = 0;    // path number (1-based) where this layer currently ends
};
