#pragma once

// GcodeForge's custom dark theme -- a neutral charcoal palette with one
// muted-blue accent used consistently for anything "on/active/draggable"
// (buttons, checkmarks, active tabs, docking previews), in the spirit of
// professional CAD/DCC tools (Blender, Fusion 360) rather than ImGui's
// generic default dark theme. Call once, after ImGui::CreateContext(),
// instead of ImGui::StyleColorsDark().
//
// Deliberately does NOT touch the semantic severity colors used inline
// elsewhere (the Export dialog's red/amber critical/warning text,
// PathColorizer's speed heatmap) -- those are meaningful data, not
// chrome, and stay exactly as they are regardless of theme.
void applyGcodeForgeTheme();
