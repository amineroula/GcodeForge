#pragma once

#include "render/BedSettings.h"

#include <string>

// Persists BedSettings to/from a small plain-text file ("key value" per
// line) -- deliberately simple, not a general project format (that's
// docs/PLAN.md milestone 12's job). Lets an operator save one bed
// configuration and reuse it across sessions/files without retyping
// width/depth/origin/grid-spacing every time.
bool saveBedSettings(const std::string& path, const BedSettings& bed);
bool loadBedSettings(const std::string& path, BedSettings& bed);
