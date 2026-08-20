#pragma once

#include "model/BedHeightmap.h"
#include "render/BedSettings.h"

#include <string>

// Persists BedSettings (and, since bed measurements are naturally saved
// together with the bed they were taken on, the BedHeightmap too) to/from
// a small plain-text file ("key value" per line) -- deliberately simple,
// not a general project format (that's docs/PLAN.md milestone 12's job).
// Lets an operator save one bed configuration and reuse it across
// sessions/files without retyping width/depth/origin/grid-spacing --  or
// re-entering every elevation measurement -- every time.
bool saveBedSettings(const std::string& path, const BedSettings& bed, const BedHeightmap& heightmap);
bool loadBedSettings(const std::string& path, BedSettings& bed, BedHeightmap& heightmap);
