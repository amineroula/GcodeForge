#pragma once

#include "model/Scene.h"
#include "render/PathColorizer.h"

// All ImGui panels: menu bar, object list, transform, layer table,
// selection groups, speed editing, color mode picker. Owns only UI input
// state (text buffers, pending values) -- actual scene mutation goes
// through editor/Selection.h and editor/SpeedEditing.h so the same logic
// stays testable without ImGui in the loop (see tests/parser_smoke_test.cpp).
class EditorUI {
public:
    // Draws every panel for the current frame. Sets `sceneDirty` to true if
    // anything changed that requires SceneRenderer::rebuild() (transform
    // edit, visibility toggle, color mode change, etc).
    void draw(Scene& scene, ColorMode& colorMode, size_t renderedLineCount, bool& sceneDirty);

    bool openFileRequested() const { return openFileRequested_; }
    void clearOpenFileRequest() { openFileRequested_ = false; }

private:
    void drawMenuBar();
    void drawObjectListPanel(Scene& scene, bool& dirty);
    void drawTransformPanel(SceneObject& object, bool& dirty);
    void drawLayerTablePanel(SceneObject& object, bool& dirty);
    void drawSelectionGroupPanel(SceneObject& object, bool& dirty);
    void drawSpeedPanel(SceneObject& object, bool& dirty);
    void drawColorModePanel(ColorMode& colorMode, bool& dirty);
    void drawStatsPanel(const Scene& scene, size_t renderedLineCount);

    bool openFileRequested_ = false;

    double speedExact_ = 0.040;
    double speedPercent_ = 10.0;

    char groupNameBuffer_[64] = "Group";
    float groupColor_[3] = {0.212f, 0.663f, 1.0f};
};
