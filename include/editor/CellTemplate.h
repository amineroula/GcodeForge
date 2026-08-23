#pragma once

#include "model/SceneObject.h"
#include "render/BedSettings.h"

#include <optional>

// Real use, reported after the interleave header/footer fix: not every
// object has real KRL boilerplate to lift in the first place. A plain
// sliced .gcode/.nc import has no &ACCESS, no safety interrupts, no
// safe-pose PTP, and no shutdown block at all -- it's just motion lines.
// Mirroring/interleaving such objects (or a set of objects that all
// happen to lack boilerplate) would fall back to a bare "DEF .../END",
// reproducing the exact bug the header/footer fix solved.
//
// The fix: capture a CellTemplate once from a real, known-good file on
// this cell (its header + footer, saved with the bed settings -- see
// render/BedSettings.h), then apply it to any object that's missing its
// own. The template's approach/retreat geometry is rigidly translated so
// it lands exactly at the target object's own first/last print position,
// preserving the template's relative shape (how far the approach lifts,
// how far the retreat steps back) regardless of where the new object
// actually sits on the bed.

// Does this object already have real header/footer boilerplate of its
// own? True only if BOTH a header (something before its first real path)
// and a footer (something after its last real path) exist. A plain
// sliced import -- or any object built from nothing, like a merge whose
// source objects all lacked boilerplate -- reports false.
bool objectHasBoilerplate(const SceneObject& object);

// Captures `object`'s own header/footer (via editor/Boilerplate.h) as a
// reusable CellTemplate, coordinate-patched into world space through the
// object's own transform. std::nullopt if the object has no boilerplate
// of its own to capture (objectHasBoilerplate() would be false).
std::optional<CellTemplate> captureCellTemplate(const SceneObject& object);

// Applies `tmpl` to `object`, which must be missing its own boilerplate
// (call objectHasBoilerplate() first -- this does not check, so it never
// silently duplicates one). Inserts the template's header before and
// footer after the object's existing content, translating both by the
// delta between the template's own anchor position (its header's last
// coordinate-bearing line; its footer's first) and the object's own
// actual first/last print position -- so the approach lands exactly at
// this object's print start and the retreat steps back from exactly
// where this object's print actually ends, in this object's own LOCAL
// space (the inverse of its transform, since Path::from/to are always
// local). Existing paths' srcLine indices are shifted to account for the
// inserted header. Also sets object.startPoint from the template's
// header's own joint-space pose, if the header lines contained one.
//
// Returns false (no-op) if `object` has no paths at all, or if `tmpl`
// isn't captured.
bool applyCellTemplate(SceneObject& object, const CellTemplate& tmpl);
