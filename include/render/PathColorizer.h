#pragma once

#include "model/SceneObject.h"

#include <glm/glm.hpp>
#include <vector>

// Reproduces the original's pathColor() exactly -- same five modes, same
// hex palette, same fallback colors -- so switching color modes in the
// port looks like switching them in the original.
enum class ColorMode {
    Object,
    Type,
    Layer,
    Group,
    Speed
};

// The original's shared 18-color palette (index.html's `palette` array),
// used for both "color by layer" and "color by speed bucket".
const std::vector<glm::vec3>& colorPalette();

// Assigns each distinct effective speed value present in the scene a
// palette color, sorted ascending -- mirrors rebuildSpeedColors(). Call
// after any speed edit, before rendering in Speed mode.
class SpeedColorTable {
public:
    void rebuild(const std::vector<SceneObject>& objects);
    glm::vec3 colorFor(double speed) const;

private:
    std::vector<double> sortedSpeeds_;
};

glm::vec3 pathColor(const SceneObject& object, const Path& path, ColorMode mode,
                     const SpeedColorTable& speedColors);
