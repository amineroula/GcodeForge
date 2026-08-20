#pragma once

#include "model/SceneObject.h"

// Splits each currently-selected path into two, inserting a new vertex at
// the midpoint -- e.g. so half of a long travel move can get its own
// speed override without affecting the other half.
//
// For each selected path P (A -> B): P itself is shortened in place to
// M -> B (keeping its number, srcLine, and layer -- so its selection
// membership, layer-table entry, etc. all stay valid), and a NEW path
// A -> M is inserted into the object's path vector immediately before it,
// with a fresh unique number and srcLine == -1 (see Path::
// cloneTemplateSrcLine -- SrcExporter synthesizes a real source line for
// it from P's original one). The new path is not added to the selection.
//
// object.layers' startPath/endPath bounds are NOT updated after a split
// (they're just display metadata in the layer table -- selection and
// export both key off Path::layer, not those bounds -- see
// editor/Selection.h's pathNumbersForLayer()), so they may read slightly
// stale until the next full reparse. Cosmetic only.
void splitSelectedPaths(SceneObject& object);
