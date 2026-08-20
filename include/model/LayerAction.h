#pragma once

#include <string>

// A user-inserted command at the start of a specific print layer -- e.g.
// "HALT" or turning a part-cooling output on/off. The actual KRL text is
// operator-supplied rather than hardcoded: this app has no way to know a
// given robot cell's actual I/O mapping (which $OUT[n] controls cooling,
// etc.), and guessing would risk generating a command that does the wrong
// thing on a real machine. Presets in the UI just pre-fill common
// boilerplate; the operator confirms/edits the real command before export.
struct LayerAction {
    int layer = 0;
    std::string label;   // shown in the UI, e.g. "Part cooling ON"
    std::string krlText; // the actual KRL line(s) inserted before the layer's first motion line
};
