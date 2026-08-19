#pragma once

#include "model/SceneObject.h"

#include <string>
#include <vector>

// Parses a KUKA SRC (KRL) file into a SceneObject. This is a direct port of
// the original's parseObject() -- same regexes, same travel-marker and
// layer-detection logic -- because the SRC parser is the load-bearing one
// (see docs/LOG.md milestone 5: the original never actually parses .gcode
// despite the product's name).
SceneObject parseSrc(const std::string& objectName, const std::vector<std::string>& lines);
