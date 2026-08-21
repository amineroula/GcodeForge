#pragma once

#include "model/SceneObject.h"

#include <glm/glm.hpp>
#include <map>
#include <vector>

// Reproduces the original's pathColor() exactly -- same five modes, same
// hex palette, same fallback colors -- so switching color modes in the
// port looks like switching them in the original.
enum class ColorMode {
    Object,
    Type,
    Layer,
    Group,
    Speed,
    // Colors each path by WHERE IT FALLS IN THE PROGRAM (blue = printed
    // first, red = printed last). Exists because print ORDER is
    // otherwise invisible: an interleaved multi-part program and a
    // sequential one produce identical finished geometry, so a static
    // render can't distinguish "A layer 1, B layer 1, A layer 2..." from
    // "all of A, then all of B". Under this mode the difference is
    // obvious -- interleaved parts each show the FULL blue-to-red
    // gradient, sequential parts show one cool block and one warm block.
    Sequence
};

// The original's shared 18-color palette (index.html's `palette` array),
// used for both "color by layer" and "color by speed bucket".
const std::vector<glm::vec3>& colorPalette();

// Assigns each distinct effective speed value present in the scene a
// palette color, sorted ascending -- mirrors rebuildSpeedColors(). Call
// after any speed edit, before rendering in Speed mode.
// Continuous gradient, NOT a discrete palette lookup: red (slow) ->
// green (right at the 0.6 pivot) -> blue (fast). Chosen to match the bed
// heightmap's gradient convention (also a continuous ramp through a
// meaningful midpoint) rather than an arbitrary N-color palette, so
// "which speed is which color" reads the same way across both panels.
class SpeedColorTable {
public:
    void rebuild(const std::vector<SceneObject>& objects);
    glm::vec3 colorFor(double speed) const;

    // Highest path number in a given object, cached at rebuild() time so
    // Sequence mode can normalize a path's position without rescanning
    // the object per path (that would be O(n^2) -- real files run to
    // 24k+ paths). Returns 1 for an unknown/empty object so callers can
    // divide safely.
    int maxPathNumber(int objectId) const;

private:
    std::vector<double> sortedSpeeds_;
    std::map<int, int> maxPathNumberByObject_;
};

glm::vec3 pathColor(const SceneObject& object, const Path& path, ColorMode mode,
                     const SpeedColorTable& speedColors);

// The one selection-highlight color, shared by every renderer that needs
// to show "this is selected" (SelectionHighlightRenderer's line overlay,
// GeometryRenderer's baked-in bead/travel coloring). Bright green,
// deliberately outside the normal palette so it never gets confused with
// a color-mode color.
glm::vec3 selectionHighlightColor();
