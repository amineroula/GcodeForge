#pragma once

#include "model/BedHeightmap.h"
#include "model/Scene.h"
#include "render/BedSettings.h"
#include "render/LightingSettings.h"
#include "render/PathColorizer.h"
#include "render/RenderSettings.h"

#include <string>

// A whole editing session, saved and restored: every object with its
// paths, transforms, speed overrides, selections, groups, layer actions
// and start point, plus the bed, heightmap, lighting, render settings and
// colour mode.
//
// Why this exists separately from SRC export: a .src file is a robot
// program, and deliberately carries only what the robot needs. Everything
// the EDITOR knows that the robot doesn't -- which paths are selected,
// what the selection groups are called and coloured, where the bed is,
// the measured bed heightmap, the safe point, which objects are mirrors
// of which, the pending object links, per-object colours -- has no place
// to live in a .src and is lost the moment the app closes. Re-deriving it
// by re-importing the SRC is impossible: the information was never in
// there to begin with.
//
// Format: plain-text, one record per line, "key value..." like BedIO's.
// Deliberately not JSON -- no dependency, diffable in git, and repairable
// by hand if a session file is ever half-written. Sections are delimited
// by OBJECT/ENDOBJECT so a malformed record can be skipped without
// dragging the rest of the file down with it.
struct ProjectData {
    Scene scene;
    BedSettings bed;
    BedHeightmap heightmap;
    LightingSettings lighting;
    RenderSettings render;
    ColorMode colorMode = ColorMode::Layer;
};

bool saveProject(const std::string& path, const ProjectData& project);

// On failure `project` is left untouched, so a failed load can never
// half-destroy the session already open.
bool loadProject(const std::string& path, ProjectData& project);
