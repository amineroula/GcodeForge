#pragma once

#include "model/PrintCalibrationGrid.h"
#include "model/PrintCalibrationRecord.h"
#include "model/SceneObject.h"
#include "render/BedSettings.h"

// Samples PrintCalibrationGrid's measured bead-width ERROR (measured -
// golden) at an arbitrary WORLD-space (x, y) via bilinear interpolation
// between the four surrounding grid points -- same technique as
// editor/BedConform.h's sampleBedElevation, applied to a grid of measured
// print results instead of probed bed elevation. Returns 0 if the grid
// has no valid size (cols/rows < 2).
double sampleWidthError(const PrintCalibrationGrid& grid, const BedSettings& bed, double worldX, double worldY);

struct PrintCalibrationOptions {
    // How many bottom layers (by Layer::layer, 1-based) get compensated,
    // tapering linearly from full strength at layer 1 to zero at layer
    // (affectedLayers + 1) and beyond -- same taper as BedConformOptions,
    // since the physical cause (nozzle-to-bed distance) matters most for
    // the first few layers.
    int affectedLayers = 1;
    bool adjustZ = true;
    bool adjustSpeed = true;
    // mm of Z shift per mm of measured width error, negated -- a bead
    // measured WIDER than golden implies the nozzle was effectively too
    // CLOSE to the bed (over-squished, wide and short), so a positive
    // error raises the nozzle (+Z); a NARROWER bead implies too far, so a
    // negative error lowers it (-Z). This is a physical starting
    // assumption (squish ratio ~1:1), not a measured constant -- expected
    // to be refined once a corrected reprint's residual error is known.
    double zGainPerMmError = 1.0;
    // Effective-speed multiplier per mm of width error -- a WIDER-than-
    // golden bead runs FASTER to thin it back down (opposite sign
    // convention from BedConformOptions::speedGainPerMm, which responds
    // to bed elevation, not measured width: here more material laid down
    // means the fix is to move faster, not slower).
    double speedGainPerMmError = 0.05;
};

// Applies print calibration to every PRINT path in `object` (travel paths
// are skipped) and returns a re-scalable, revertible PrintCalibrationRecord
// -- the caller stores it as `object.printCalibration`. If a calibration
// record is ALREADY active, it must be removed first (removePrintCalibration)
// so the fresh apply computes from the object's true pre-calibration state.
PrintCalibrationRecord applyPrintCalibrationRecorded(SceneObject& object, const PrintCalibrationGrid& grid,
                                                      const BedSettings& bed, const PrintCalibrationOptions& options);

// Re-scales an ACTIVE calibration's effect strength (1.0 = as originally
// applied, 0.0 = fully reverted without removing the record). Recomputes
// every affected path from its stored pre-calibration baseline + newScale
// * the stored per-path delta -- never compounds. No-op if
// `object.printCalibration` isn't set.
void setPrintCalibrationScale(SceneObject& object, double newScale);

// Reverts every path this calibration affected back to its exact
// pre-calibration Z/speed and clears `object.printCalibration`. No-op if
// not set.
void removePrintCalibration(SceneObject& object);

// Keeps the CURRENTLY COMPUTED Z/speed values exactly as they are and
// clears `object.printCalibration` so it's no longer independently
// adjustable/removable. No-op if not set.
void bakePrintCalibration(SceneObject& object);
