#pragma once

// Self-contained vector icons for the toolbar, drawn with plain
// ImDrawList primitives -- no icon font, no image asset, no new
// FetchContent dependency. Each is a simple, unambiguous glyph rather
// than an illustration; clarity at 24px matters more than detail.
namespace Icons {

enum class Id {
    Open,
    Save,
    Undo,
    Redo,
    Move,
    Rotate,
    FrameAll,
    Grid,
    Geometry, // toggles Lines <-> Geometry render mode
    Speed,
    GizmoObject,   // move gizmo target: whole object
    GizmoStart,    // move gizmo target: selected paths' start point
    GizmoEnd,      // move gizmo target: selected paths' end point
    GizmoWhole,    // move gizmo target: selected paths, rigidly
    ColorObject,   // color mode: solid per-object color
    ColorType,     // color mode: print vs travel
    ColorLayer,    // color mode: by layer
    ColorGroup,    // color mode: by selection group
    ColorSpeed,    // color mode: speed heatmap
    ColorSequence, // color mode: print order gradient
};

// Draws a square icon button of `size` px and returns true if clicked
// this frame. `active` renders the icon in the theme's accent color
// (for a toggle that's currently "on," e.g. the current gizmo mode or
// render mode) instead of plain text color. `enabled=false` dims it and
// ignores clicks, mirroring ImGui::BeginDisabled without requiring the
// caller to wrap every call site.
bool IconButton(Id icon, float size, bool active = false, bool enabled = true, const char* tooltip = nullptr);

} // namespace Icons
