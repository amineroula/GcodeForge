#pragma once

#include "editor/UndoStack.h"
#include "model/Scene.h"
#include "render/BedSettings.h"
#include "render/Camera.h"
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
              BedSettings& bedSettings, UndoStack& undoStack, size_t renderedPrimitiveCount,
              bool& sceneDirty, bool& selectionDirty, bool& bedDirty);

    bool openFileRequested() const { return openFileRequested_; }
    void clearOpenFileRequest() { openFileRequested_ = false; }

    bool saveBedRequested() const { return saveBedRequested_; }
    void clearSaveBedRequest() { saveBedRequested_ = false; }
    bool loadBedRequested() const { return loadBedRequested_; }
    void clearLoadBedRequest() { loadBedRequested_ = false; }

    // Set once, right after construction, if main.cpp successfully loaded
    // a bold font (see main.cpp's font setup). Section labels use it when
    // set; when null, they just render in the regular font -- the app
    // still works, it's a cosmetic upgrade, not a dependency.
    void setBoldFont(ImFont* font) { boldFont_ = font; }

private:
    void sectionLabel(const char* text); // bold if boldFont_ is set, plain otherwise

    void drawMenuBar(Scene& scene, UndoStack& undoStack, bool& sceneDirty);
    void drawViewPanel(Camera& camera, RenderSettings& renderSettings, bool& dirty);
    void drawBedPanel(BedSettings& bedSettings, bool& bedDirty);
    void drawObjectListPanel(Scene& scene, UndoStack& undoStack, bool& dirty);
    void drawTransformPanel(Scene& scene, SceneObject& object, UndoStack& undoStack, bool& dirty);
    void drawLayerTablePanel(SceneObject& object, bool& selectionDirty);
    void drawSelectionGroupPanel(Scene& scene, SceneObject& object, UndoStack& undoStack, bool& dirty, bool& selectionDirty);
    void drawSpeedPanel(Scene& scene, SceneObject& object, UndoStack& undoStack, bool& dirty);
    void drawColorModePanel(ColorMode& colorMode, bool& dirty);
    void drawStatsPanel(const Scene& scene, RenderMode mode, size_t renderedPrimitiveCount);

    bool openFileRequested_ = false;
    bool saveBedRequested_ = false;
    bool loadBedRequested_ = false;

    double speedExact_ = 0.040;
    double speedPercent_ = 10.0;

    char groupNameBuffer_[64] = "Group";
    float groupColor_[3] = {0.212f, 0.663f, 1.0f};

    ImFont* boldFont_ = nullptr;
};
