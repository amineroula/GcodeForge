#pragma once

#include "editor/UndoStack.h"
#include "model/BedHeightmap.h"
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
    void draw(Scene& scene, ColorMode& colorMode, Camera& camera, RenderSettings& renderSettings,
              BedSettings& bedSettings, LightingSettings& lightingSettings, BedHeightmap& bedHeightmap,
              UndoStack& undoStack, size_t renderedPrimitiveCount, bool& sceneDirty, bool& selectionDirty, bool& bedDirty);

    bool openFileRequested() const { return openFileRequested_; }
    void clearOpenFileRequest() { openFileRequested_ = false; }

    bool saveSrcRequested() const { return saveSrcRequested_; }
    void clearSaveSrcRequest() { saveSrcRequested_ = false; }

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
    void drawViewPanel(Camera& camera, RenderSettings& renderSettings, bool& dirty);
    void drawBedPanel(BedSettings& bedSettings, LightingSettings& lightingSettings, BedHeightmap& heightmap,
                       SceneObject* activeObject, bool& bedDirty);
    void drawObjectListPanel(Scene& scene, UndoStack& undoStack, bool& dirty);
    void drawMultiPartPanel(Scene& scene, UndoStack& undoStack, bool& dirty);
    void drawTransformPanel(Scene& scene, SceneObject& object, UndoStack& undoStack, bool& dirty);
    void drawLayerTablePanel(Scene& scene, SceneObject& object, UndoStack& undoStack, bool& dirty, bool& selectionDirty);
    void drawSelectionGroupPanel(Scene& scene, SceneObject& object, UndoStack& undoStack, bool& dirty, bool& selectionDirty);
    void drawSpeedPanel(Scene& scene, SceneObject& object, UndoStack& undoStack, bool& dirty);
    void drawBedConformPanel(Scene& scene, SceneObject& object, const BedHeightmap& heightmap,
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
    bool saveSrcRequested_ = false;
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

    // Multi-part / interleaved-cooling input state -- see
    // editor/MirrorObject.h and editor/InterleavePrint.h.
    int multiPartCopies_ = 2;
    float multiPartSafeDistanceMm_ = 200.0f;
    float multiPartTravelClearanceMm_ = 50.0f;
    float multiPartTravelSpeed_ = 0.5f;
    std::string lastMirrorResult_; // feedback line so the button visibly does something even when the result is off-screen

    float rotateSelectedAngleDeg_ = 90.0f;
};
