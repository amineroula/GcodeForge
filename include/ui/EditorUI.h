#pragma once

#include "model/Scene.h"
#include "render/Camera.h"
#include "render/PathColorizer.h"
#include "render/RenderSettings.h"

// All ImGui panels: menu bar, view/projection controls, object list,
// transform, layer table, selection groups, speed editing, color mode
// picker. Owns only UI input state (text buffers, pending values) -- actual
// scene mutation goes through editor/Selection.h and editor/SpeedEditing.h
// so the same logic stays testable without ImGui in the loop (see
// tests/parser_smoke_test.cpp).
class EditorUI {
public:
    // Draws every panel for the current frame. Sets `sceneDirty` to true if
    // anything changed that requires the active renderer to rebuild
    // (transform edit, visibility toggle, color mode change, render mode
    // switch, bead size change, etc). Camera changes (view presets,
    // projection toggle) don't need a rebuild -- they only affect the
    // matrices main.cpp already recomputes every frame.
    void draw(Scene& scene, ColorMode& colorMode, Camera& camera, RenderSettings& renderSettings,
              size_t renderedPrimitiveCount, bool& sceneDirty);

    bool openFileRequested() const { return openFileRequested_; }
    void clearOpenFileRequest() { openFileRequested_ = false; }

private:
    void drawMenuBar();
    void drawViewPanel(Camera& camera, RenderSettings& renderSettings, bool& dirty);
    void drawObjectListPanel(Scene& scene, bool& dirty);
    void drawTransformPanel(SceneObject& object, bool& dirty);
    void drawLayerTablePanel(SceneObject& object, bool& dirty);
    void drawSelectionGroupPanel(SceneObject& object, bool& dirty);
    void drawSpeedPanel(SceneObject& object, bool& dirty);
    void drawColorModePanel(ColorMode& colorMode, bool& dirty);
    void drawStatsPanel(const Scene& scene, RenderMode mode, size_t renderedPrimitiveCount);

    bool openFileRequested_ = false;

    double speedExact_ = 0.040;
    double speedPercent_ = 10.0;

    char groupNameBuffer_[64] = "Group";
    float groupColor_[3] = {0.212f, 0.663f, 1.0f};
};
