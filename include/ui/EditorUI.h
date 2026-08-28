#pragma once

#include "editor/ExportValidation.h"
#include "editor/Gizmo.h"
#include "editor/UndoStack.h"
#include "model/BedHeightmap.h"
#include "model/PrintCalibrationGrid.h"
#include <set>
#include <string>
#include "model/Scene.h"
#include "render/BedSettings.h"
#include "render/Camera.h"
#include "render/LightingSettings.h"
#include "render/PathColorizer.h"
#include "render/RenderSettings.h"

struct ImFont;

// All ImGui panels: menu bar, view/projection controls, object list,
// transform, layer table, selection groups, speed editing, color mode
// picker, and a separate Bed panel (right side). Owns only UI input state
// (text buffers, pending values) -- actual scene mutation goes through
// editor/Selection.h, editor/SpeedEditing.h, and editor/UndoStack.h so the
// same logic stays testable without ImGui in the loop (see
// tests/parser_smoke_test.cpp).
class EditorUI {
public:
    // Draws every panel for the current frame.
    //   sceneDirty     -- a structural change happened (transform, speed,
    //                     visibility, color mode, render mode...) that
    //                     needs the active renderer to fully rebuild.
    //   selectionDirty -- ONLY the selection changed (layer-table click,
    //                     group select, clear). Deliberately kept separate
    //                     from sceneDirty: re-selecting paths is a common,
    //                     lightweight action that should only refresh the
    //                     highlight overlay, not rebuild the whole scene's
    //                     geometry -- doing the latter was an unnecessary
    //                     cost on large files.
    //   bedDirty       -- bed settings changed (rebuilds the grid only).
    // Camera-only changes (view presets, projection toggle) need none of
    // these -- they just affect matrices main.cpp already recomputes every
    // frame.
    // gizmoMode: read-only, so the toolbar's Move/Rotate icons can show
    // which one is CURRENTLY active -- main.cpp owns the actual state
    // (it's read every frame by the gizmo-drag hit-testing code, not
    // something EditorUI has any reason to own itself); toggling it goes
    // through moveToolRequested()/rotateToolRequested() below instead.
    void draw(Scene& scene, ColorMode& colorMode, Camera& camera, RenderSettings& renderSettings,
              BedSettings& bedSettings, LightingSettings& lightingSettings, BedHeightmap& bedHeightmap,
              PrintCalibrationGrid& printCalibrationGrid,
              UndoStack& undoStack, size_t renderedPrimitiveCount, bool& sceneDirty, bool& selectionDirty, bool& bedDirty,
              GizmoInteractionMode gizmoMode);

    bool openFileRequested() const { return openFileRequested_; }
    void clearOpenFileRequest() { openFileRequested_ = false; }

    // Toolbar actions that need to reach OUTSIDE EditorUI's own state
    // (gizmo interaction mode and camera framing both live in main.cpp's
    // render loop) -- same request-flag pattern as every other
    // cross-boundary action (openFileRequested_ etc.).
    bool moveToolRequested() const { return moveToolRequested_; }
    void clearMoveToolRequest() { moveToolRequested_ = false; }
    bool rotateToolRequested() const { return rotateToolRequested_; }
    void clearRotateToolRequest() { rotateToolRequested_ = false; }
    bool frameAllRequested() const { return frameAllRequested_; }
    void clearFrameAllRequest() { frameAllRequested_ = false; }
    bool toggleGridRequested() const { return toggleGridRequested_; }
    void clearToggleGridRequest() { toggleGridRequested_ = false; }

    // "Save SRC As..." opens this dialog instead of saving straight away
    // (editor/ExportValidation.h's individually-runnable checks) --
    // requested from real use: an explicit menu to set export options
    // (currently just speed rounding), run each structural/speed check on
    // its own or all at once, see the combined report, THEN save. main.cpp
    // owns the actual file write (it needs the native save dialog + a
    // GLFWwindow*, neither of which EditorUI has); this class only decides
    // WHETHER and WITH WHAT OPTIONS to save.
    bool exportSaveRequested() const { return exportSaveRequested_; }
    void clearExportSaveRequest() { exportSaveRequested_ = false; }
    bool exportRoundSpeedsTo4Decimals() const { return exportRoundSpeeds_; }

    // Print animation panel. main.cpp owns the actual AnimationSequence,
    // GL renderers, and playback clock (this class stays GL-free) -- it
    // reads user-editable settings from the struct below, and passes the
    // computed readout (current/total time, running/finished) back in
    // each frame so the timeline slider and HUD text stay in sync with
    // whatever's actually playing.
    struct AnimationSettings {
        bool includePrint = true;
        bool includeTravel = true;
        float maxSegmentLengthMm = 5.0f; // long straight paths split into pieces this long or shorter, for a gradual reveal
        float fallbackSpeedMps = 0.04f;  // used for any path with no recorded speed
        float speedMultiplier = 1.0f;    // 1.0 = real time
        bool showHead = true;
        float headWidthMm = 180.0f;
        float headDepthMm = 140.0f;
        float headHeightMm = 170.0f;
        float nozzleLengthMm = 90.0f;
        float nozzleWidthMm = 35.0f;
    };
    // Read/write settings (speed, subdivision length, head size...) main.cpp
    // uses when building the sequence and stepping playback.
    AnimationSettings& animationSettings() { return animationSettings_; }

    // Playback readout -- main.cpp calls this once per frame with
    // whatever it just computed, same pattern as setHoverInfo().
    void setAnimationReadout(double currentTimeSeconds, double totalTimeSeconds, bool running, bool finished, bool built) {
        animCurrentTime_ = currentTimeSeconds;
        animTotalTime_ = totalTimeSeconds;
        animRunning_ = running;
        animFinished_ = finished;
        animBuilt_ = built;
    }

    bool animationBuildRequested() const { return animationBuildRequested_; }
    void clearAnimationBuildRequest() { animationBuildRequested_ = false; }
    bool animationPlayRequested() const { return animationPlayRequested_; }
    void clearAnimationPlayRequest() { animationPlayRequested_ = false; }
    bool animationPauseRequested() const { return animationPauseRequested_; }
    void clearAnimationPauseRequest() { animationPauseRequested_ = false; }
    bool animationStopRequested() const { return animationStopRequested_; }
    void clearAnimationStopRequest() { animationStopRequested_ = false; }
    // Stop only pauses/rewinds playback -- it does NOT exit simulation
    // mode, which suppresses the normal object view entirely while built
    // (see main.cpp's draw dispatch). Reported from real use: "when I do
    // the animation and want to go back to my file I can't" -- Stop alone
    // left the user staring at a blank/reveal-in-progress mesh with no
    // discoverable way back. This is the explicit exit.
    bool animationExitRequested() const { return animationExitRequested_; }
    void clearAnimationExitRequest() { animationExitRequested_ = false; }
    bool animationScrubbed() const { return animationScrubbed_; }
    double animationScrubTimeSeconds() const { return animationScrubTimeSeconds_; }
    void clearAnimationScrub() { animationScrubbed_ = false; }

    bool saveBedRequested() const { return saveBedRequested_; }
    void clearSaveBedRequest() { saveBedRequested_ = false; }
    bool loadBedRequested() const { return loadBedRequested_; }
    void clearLoadBedRequest() { loadBedRequested_ = false; }

    bool saveProjectRequested() const { return saveProjectRequested_; }
    void clearSaveProjectRequest() { saveProjectRequested_ = false; }
    bool loadProjectRequested() const { return loadProjectRequested_; }
    void clearLoadProjectRequest() { loadProjectRequested_ = false; }

    // Set once, right after construction, if main.cpp successfully loaded
    // a bold font (see main.cpp's font setup). Section labels use it when
    // set; when null, they just render in the regular font -- the app
    // still works, it's a cosmetic upgrade, not a dependency.
    void setBoldFont(ImFont* font) { boldFont_ = font; }

    // Show/hide both floating panels. Driven by the menu-bar button and
    // by the Tab key (see main.cpp's key handling).
    void togglePanels() { panelsVisible_ = !panelsVisible_; }
    bool panelsVisible() const { return panelsVisible_; }

    // Status-bar readout: what the cursor is currently over. Passing the
    // resolved layer/speed rather than a PathRef keeps EditorUI free of
    // the picking/scene-lookup logic that main.cpp already does.
    struct HoverInfo {
        bool valid = false;
        std::string objectName;
        int pathNumber = 0;
        int layer = -1;
        double speed = 0.0;
        bool isTravel = false;
    };
    void setHoverInfo(const HoverInfo& info) { hoverInfo_ = info; }

private:
    void sectionLabel(const char* text); // bold if boldFont_ is set, plain otherwise

    void drawMenuBar(Scene& scene, UndoStack& undoStack, bool& sceneDirty);
    void drawStatusBar();
    void drawExportDialog(Scene& scene);
    void drawAnimationPanel();
    void drawViewPanel(Camera& camera, RenderSettings& renderSettings, bool& dirty);
    void drawBedPanel(BedSettings& bedSettings, LightingSettings& lightingSettings, BedHeightmap& heightmap,
                       PrintCalibrationGrid& calibrationGrid, SceneObject* activeObject, bool& bedDirty);
    void drawLightingPanel(LightingSettings& lightingSettings);
    void drawObjectListPanel(Scene& scene, UndoStack& undoStack, bool& dirty);
    void drawMultiPartPanel(Scene& scene, UndoStack& undoStack, bool& dirty);
    void drawCommentPanel(SceneObject& object);
    void drawTransformPanel(Scene& scene, SceneObject& object, UndoStack& undoStack, bool& dirty);
    void drawLayerTablePanel(Scene& scene, SceneObject& object, UndoStack& undoStack, bool& dirty, bool& selectionDirty);
    void drawSelectionGroupPanel(Scene& scene, SceneObject& object, UndoStack& undoStack, bool& dirty, bool& selectionDirty);
    void drawSpeedPanel(Scene& scene, SceneObject& object, UndoStack& undoStack, bool& dirty);
    void drawBedConformPanel(Scene& scene, SceneObject& object, const BedHeightmap& heightmap,
                              const BedSettings& bed, UndoStack& undoStack, bool& dirty);
    void drawPrintCalibrationPanel(Scene& scene, SceneObject& object, const PrintCalibrationGrid& grid,
                                    const BedSettings& bed, UndoStack& undoStack, bool& dirty);
    // The "check" and "fix" the user asked for after seeing an interleaved
    // export that had lost its whole safety header and shutdown footer --
    // and a heads-up about what's coming next: a plain sliced import has
    // NO boilerplate of its own at all. Warns when `object` is missing it
    // (editor/CellTemplate.h's objectHasBoilerplate()) and offers a "Fix"
    // button that applies the cell-level template captured via the Bed
    // panel (drawBedPanel's own capture button, since the template itself
    // is cell-level, not object-level -- see render/BedSettings.h).
    void drawCellTemplatePanel(Scene& scene, SceneObject& object, const BedSettings& bed,
                                UndoStack& undoStack, bool& dirty);
    void drawColorModePanel(ColorMode& colorMode, bool& dirty);
    void drawStatsPanel(const Scene& scene, RenderMode mode, size_t renderedPrimitiveCount);

    bool openFileRequested_ = false;
    bool saveBedRequested_ = false;
    bool loadBedRequested_ = false;

    // Export SRC dialog state. One CheckOutcome per exportValidationChecks()
    // entry, index-aligned; `run=false` until that check's button (or "Run
    // all") has actually been clicked -- lets the report distinguish "this
    // passed" from "this was never checked."
    bool exportDialogOpen_ = false;
    bool exportSaveRequested_ = false;
    bool exportRoundSpeeds_ = false;
    struct CheckOutcome {
        bool run = false;
        std::vector<ValidationIssue> issues;
    };
    std::vector<CheckOutcome> exportCheckOutcomes_;

    bool animationBuildRequested_ = false;
    bool animationPlayRequested_ = false;
    bool animationPauseRequested_ = false;
    bool animationStopRequested_ = false;
    bool animationExitRequested_ = false;
    bool animationScrubbed_ = false;
    double animationScrubTimeSeconds_ = 0.0;
    AnimationSettings animationSettings_;
    double animCurrentTime_ = 0.0;
    double animTotalTime_ = 0.0;
    bool animRunning_ = false;
    bool animFinished_ = false;
    bool animBuilt_ = false;
    bool saveProjectRequested_ = false;
    bool loadProjectRequested_ = false;

    double speedExact_ = 0.040;
    double speedPercent_ = 10.0;

    char groupNameBuffer_[64] = "Group";
    float groupColor_[3] = {0.212f, 0.663f, 1.0f};

    // Layer-action input state: which preset is selected and its editable
    // KRL text, kept live so switching presets pre-fills a sane starting
    // point the operator can still edit before adding it.
    int layerActionPresetIndex_ = 0;
    char layerActionTextBuffer_[256] = "";
    int layerActionTargetLayer_ = 1;

    // Which digital output drives part cooling / air on this cell.
    // Default 5 matches the mapping an Eidos program states for the
    // operator's cell ("; AIR COMMAND" immediately above "$OUT[5]=FALSE"
    // in its shutdown block). Editable because I/O assignment is
    // per-cell, and firing the wrong output elsewhere could do harm.
    int coolingOutputIndex_ = 5;

    // Last layer-table row clicked WITHOUT shift (plain or ctrl) -- the
    // anchor a subsequent shift-click ranges from. -1 = no anchor yet.
    // Not reset on active-object switch: a stale anchor from a different
    // object just produces an odd range on the very first shift-click
    // after switching, which is an acceptable edge case for a UI cursor.
    int layerSelectionAnchor_ = -1;

    // Layer Z Offset input state -- see editor/LayerZOffset.h's
    // ZOffsetOptions (mirrored here since ImGui widgets need plain
    // persistent fields, not a struct rebuilt fresh every frame).
    // zOffsetModeIndex_ order MUST match ZOffsetMode's enum order --
    // static_cast directly from the combo index.
    // Comment text box state -- see drawCommentPanel(). Only resyncs from
    // object.comment when the active object id changes.
    char commentBuffer_[2048] = "";
    int commentBufferObjectId_ = -1;

    int zOffsetStartLayer_ = 1;
    double zOffsetDeltaMm_ = 0.0;
    int zOffsetModeIndex_ = 0;
    int zOffsetLayerCount_ = 1;

    // Dockable "big button" launcher windows -- see drawToolbar() and
    // draw()'s window dispatch. Each bool is this window's current
    // visibility; the launcher row toggles them, and each window's own
    // titlebar close button also clears its bool (via the `&bool` p_open
    // parameter every ImGui::Begin already supports).
    bool windowObjectLayersOpen_ = true;
    bool windowBedOpen_ = false;
    bool windowAdvancedSpeedOpen_ = false;
    bool windowMirrorLinkOpen_ = false;
    bool windowGeometryOpen_ = false;
    bool windowLightsPreviewOpen_ = false;
    bool windowAnimationOpen_ = true;

    bool moveToolRequested_ = false;
    bool rotateToolRequested_ = false;
    bool frameAllRequested_ = false;
    bool toggleGridRequested_ = false;

    void drawToolbar(Scene& scene, Camera& camera, RenderSettings& renderSettings, UndoStack& undoStack, bool& sceneDirty,
                      GizmoInteractionMode gizmoMode);

    ImFont* boldFont_ = nullptr;

    // Toggled by the "Hide/Show panels" button in the menu bar -- when
    // false, draw() returns right after drawing the menu bar, skipping
    // the Editor and Bed windows entirely for an unobstructed viewport.
    bool panelsVisible_ = true;

    HoverInfo hoverInfo_;

    // Bed Conform input state -- see editor/BedConform.h's
    // BedConformOptions (mirrored here since ImGui widgets need plain
    // persistent fields to edit, not a struct rebuilt fresh every frame).
    int bedConformAffectedLayers_ = 1;
    bool bedConformAdjustZ_ = true;
    bool bedConformAdjustSpeed_ = true;
    float bedConformSpeedGainPerMm_ = 0.05f;

    // Print Calibration input state -- see editor/PrintCalibration.h's
    // PrintCalibrationOptions, same reasoning as the Bed Conform fields
    // above.
    int printCalAffectedLayers_ = 1;
    bool printCalAdjustZ_ = true;
    bool printCalAdjustSpeed_ = true;
    float printCalZGainPerMmError_ = 1.0f;
    float printCalSpeedGainPerMmError_ = 0.05f;

    // Heightmap click-to-paint tool state -- read by main.cpp's click
    // handler (via the getters below) to decide whether a viewport click
    // should nudge a heightmap vertex instead of picking a path.
public:
    // Interaction is a plain modifier key (click = raise, Alt+click =
    // lower), not a separate mode to toggle -- matches this app's other
    // click-vs-alt-click conventions (camera orbit/pan/zoom).
    bool heightmapPaintModeActive() const { return heightmapPaintMode_; }
    float heightmapPaintPowerMm() const { return heightmapPaintPowerMm_; }
private:
    bool heightmapPaintMode_ = false;
    float heightmapPaintPowerMm_ = 0.5f;

    // Multi-part / interleaved-cooling input state -- see
    // editor/MirrorObject.h and editor/InterleavePrint.h.
    int multiPartCopies_ = 2;
    float multiPartSafeDistanceMm_ = 200.0f;
    float multiPartTravelClearanceMm_ = 50.0f;
    float multiPartTravelSpeed_ = 0.5f;
    std::string lastMirrorResult_; // feedback line so the button visibly does something even when the result is off-screen

    float rotateSelectedAngleDeg_ = 90.0f;
};
