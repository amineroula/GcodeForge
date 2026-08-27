#include "ui/Icons.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <cmath>

namespace Icons {

namespace {

void drawOpen(ImDrawList* dl, ImVec2 c, float s, ImU32 col) {
    // Folder: a body rect with a small tab on top-left.
    float w = s * 0.7f, h = s * 0.5f;
    ImVec2 p0(c.x - w * 0.5f, c.y - h * 0.15f);
    ImVec2 p1(c.x + w * 0.5f, c.y + h * 0.85f);
    dl->AddRect(p0, p1, col, 2.0f, 0, 1.6f);
    ImVec2 t0(p0.x, p0.y - h * 0.35f);
    ImVec2 t1(p0.x + w * 0.4f, p0.y);
    dl->AddRect(t0, t1, col, 2.0f, 0, 1.6f);
}

void drawSave(ImDrawList* dl, ImVec2 c, float s, ImU32 col) {
    // Floppy disk outline with a notch top-right and a slot near the bottom.
    float half = s * 0.32f;
    ImVec2 p0(c.x - half, c.y - half);
    ImVec2 p1(c.x + half, c.y + half);
    dl->AddRect(p0, p1, col, 2.0f, 0, 1.6f);
    dl->AddLine(ImVec2(p1.x - half * 0.7f, p0.y), ImVec2(p1.x, p0.y + half * 0.7f), col, 1.6f);
    dl->AddRect(ImVec2(c.x - half * 0.5f, p1.y - half * 0.6f), ImVec2(c.x + half * 0.5f, p1.y - half * 0.1f), col, 0.0f, 0, 1.4f);
}

void drawCurvedArrow(ImDrawList* dl, ImVec2 c, float s, ImU32 col, bool mirrored) {
    float r = s * 0.32f;
    float dir = mirrored ? -1.0f : 1.0f;
    float startAngle = mirrored ? 3.66f : -0.52f;
    float endAngle = mirrored ? 6.5f : 3.66f;
    dl->PathArcTo(c, r, startAngle, endAngle, 20);
    dl->PathStroke(col, 0, 1.8f);
    ImVec2 tip(c.x + dir * r * 0.95f, c.y - r * 0.25f);
    ImVec2 a(tip.x - dir * s * 0.16f, tip.y - s * 0.12f);
    ImVec2 b(tip.x - dir * s * 0.02f, tip.y + s * 0.14f);
    dl->AddTriangleFilled(tip, a, b, col);
}

void drawMove(ImDrawList* dl, ImVec2 c, float s, ImU32 col) {
    // 4-way cross with arrowheads, like a real move-gizmo glyph.
    float r = s * 0.36f;
    float head = s * 0.1f;
    const ImVec2 dirs[4] = {ImVec2(1, 0), ImVec2(-1, 0), ImVec2(0, 1), ImVec2(0, -1)};
    for (const auto& d : dirs) {
        ImVec2 tip(c.x + d.x * r, c.y + d.y * r);
        dl->AddLine(c, tip, col, 1.6f);
        ImVec2 perp(-d.y, d.x);
        ImVec2 a(tip.x - d.x * head + perp.x * head * 0.6f, tip.y - d.y * head + perp.y * head * 0.6f);
        ImVec2 b(tip.x - d.x * head - perp.x * head * 0.6f, tip.y - d.y * head - perp.y * head * 0.6f);
        dl->AddTriangleFilled(tip, a, b, col);
    }
}

void drawRotate(ImDrawList* dl, ImVec2 c, float s, ImU32 col) {
    float r = s * 0.32f;
    dl->PathArcTo(c, r, -2.5f, 2.0f, 24);
    dl->PathStroke(col, 0, 1.8f);
    ImVec2 tip(c.x + r * std::cos(2.0f), c.y + r * std::sin(2.0f));
    ImVec2 tangent(-std::sin(2.0f), std::cos(2.0f));
    ImVec2 a(tip.x + tangent.x * s * 0.14f - std::cos(2.0f) * s * 0.05f, tip.y + tangent.y * s * 0.14f - std::sin(2.0f) * s * 0.05f);
    ImVec2 b(tip.x - tangent.x * s * 0.14f - std::cos(2.0f) * s * 0.05f, tip.y - tangent.y * s * 0.14f - std::sin(2.0f) * s * 0.05f);
    dl->AddTriangleFilled(tip, a, b, col);
}

void drawFrameAll(ImDrawList* dl, ImVec2 c, float s, ImU32 col) {
    // Four corner brackets, like a camera-frame/crop icon.
    float half = s * 0.32f;
    float len = s * 0.16f;
    ImVec2 corners[4] = {ImVec2(-half, -half), ImVec2(half, -half), ImVec2(-half, half), ImVec2(half, half)};
    for (int i = 0; i < 4; ++i) {
        ImVec2 corner(c.x + corners[i].x, c.y + corners[i].y);
        float sx = (corners[i].x < 0) ? 1.0f : -1.0f;
        float sy = (corners[i].y < 0) ? 1.0f : -1.0f;
        dl->AddLine(corner, ImVec2(corner.x + sx * len, corner.y), col, 1.8f);
        dl->AddLine(corner, ImVec2(corner.x, corner.y + sy * len), col, 1.8f);
    }
}

void drawGrid(ImDrawList* dl, ImVec2 c, float s, ImU32 col) {
    float half = s * 0.3f;
    ImVec2 p0(c.x - half, c.y - half);
    ImVec2 p1(c.x + half, c.y + half);
    dl->AddRect(p0, p1, col, 0.0f, 0, 1.4f);
    dl->AddLine(ImVec2(c.x, p0.y), ImVec2(c.x, p1.y), col, 1.2f);
    dl->AddLine(ImVec2(p0.x, c.y), ImVec2(p1.x, c.y), col, 1.2f);
}

void drawGeometry(ImDrawList* dl, ImVec2 c, float s, ImU32 col) {
    // Filled triangle -- "solid geometry" vs the Lines glyph's outline.
    float r = s * 0.34f;
    ImVec2 p0(c.x, c.y - r);
    ImVec2 p1(c.x - r * 0.87f, c.y + r * 0.5f);
    ImVec2 p2(c.x + r * 0.87f, c.y + r * 0.5f);
    dl->AddTriangleFilled(p0, p1, p2, col);
}

void drawSpeed(ImDrawList* dl, ImVec2 c, float s, ImU32 col) {
    // Speedometer: an arc with a needle.
    float r = s * 0.32f;
    dl->PathArcTo(c, r, 3.4f, 6.0f, 20);
    dl->PathStroke(col, 0, 1.8f);
    ImVec2 needleTip(c.x + r * 0.8f * std::cos(4.9f), c.y + r * 0.8f * std::sin(4.9f));
    dl->AddLine(c, needleTip, col, 1.8f);
    dl->AddCircleFilled(c, 1.8f, col);
}

void drawGizmoObject(ImDrawList* dl, ImVec2 c, float s, ImU32 col) {
    // Filled square: the gizmo moves the whole object as one block.
    float half = s * 0.28f;
    dl->AddRectFilled(ImVec2(c.x - half, c.y - half), ImVec2(c.x + half, c.y + half), col, 1.5f);
}

void drawGizmoEndpoint(ImDrawList* dl, ImVec2 c, float s, ImU32 col, bool atStart) {
    // A short path segment with the moved endpoint marked by a filled dot.
    float half = s * 0.3f;
    ImVec2 a(c.x - half, c.y), b(c.x + half, c.y);
    dl->AddLine(a, b, col, 1.6f);
    ImVec2 dot = atStart ? a : b;
    ImVec2 open = atStart ? b : a;
    dl->AddCircleFilled(dot, s * 0.11f, col);
    dl->AddCircle(open, s * 0.08f, col, 0, 1.3f);
}

void drawGizmoWhole(ImDrawList* dl, ImVec2 c, float s, ImU32 col) {
    // A thick full segment, both ends solid: the whole path moves rigidly.
    float half = s * 0.3f;
    dl->AddLine(ImVec2(c.x - half, c.y), ImVec2(c.x + half, c.y), col, 2.6f);
    dl->AddCircleFilled(ImVec2(c.x - half, c.y), s * 0.1f, col);
    dl->AddCircleFilled(ImVec2(c.x + half, c.y), s * 0.1f, col);
}

void drawColorObject(ImDrawList* dl, ImVec2 c, float s, ImU32 col) {
    dl->AddCircleFilled(c, s * 0.3f, col);
}

void drawColorType(ImDrawList* dl, ImVec2 c, float s, ImU32 col) {
    // Half-solid, half-outline circle: print (solid) vs travel (outline).
    float r = s * 0.3f;
    dl->PathArcTo(c, r, -1.5708f, 1.5708f, 16);
    dl->PathLineTo(c);
    dl->PathFillConvex(col);
    dl->AddCircle(c, r, col, 0, 1.3f);
}

void drawColorLayer(ImDrawList* dl, ImVec2 c, float s, ImU32 col) {
    // Three stacked horizontal bars, like sliced layers.
    float half = s * 0.3f;
    float step = half * 0.75f;
    for (int i = -1; i <= 1; ++i) {
        float y = c.y + i * step;
        dl->AddLine(ImVec2(c.x - half, y), ImVec2(c.x + half, y), col, 1.8f);
    }
}

void drawColorGroup(ImDrawList* dl, ImVec2 c, float s, ImU32 col) {
    // A cluster of three small dots, like grouped items.
    float r = s * 0.09f;
    float d = s * 0.18f;
    dl->AddCircleFilled(ImVec2(c.x, c.y - d), r, col);
    dl->AddCircleFilled(ImVec2(c.x - d * 0.87f, c.y + d * 0.5f), r, col);
    dl->AddCircleFilled(ImVec2(c.x + d * 0.87f, c.y + d * 0.5f), r, col);
}

void drawColorSequence(ImDrawList* dl, ImVec2 c, float s, ImU32 col) {
    // Arrow along a track of dots fading in tint, start to end.
    (void)col;
    float half = s * 0.3f;
    for (int i = 0; i < 4; ++i) {
        float t = i / 3.0f;
        ImVec2 p(c.x - half + t * 2.0f * half, c.y);
        ImU32 dotCol = IM_COL32(
            static_cast<int>(76 + t * (214 - 76)), static_cast<int>(134 + t * (90 - 134)),
            static_cast<int>(214 + t * (76 - 214)), 255);
        dl->AddCircleFilled(p, s * 0.07f, dotCol);
    }
    dl->AddLine(ImVec2(c.x - half, c.y), ImVec2(c.x + half, c.y), IM_COL32(150, 150, 150, 160), 1.2f);
}

} // namespace

bool IconButton(Id icon, float size, bool active, bool enabled, const char* tooltip) {
    // Every call previously shared the literal ID "##icon" -- fine for
    // ONE button, but with 10 of them in the same window ImGui saw 10
    // widgets fighting over one identity (visible as red conflict boxes
    // + a "Programmer error: N visible items with conflicting ID!"
    // popup, reported from real use). Keying by the icon enum gives each
    // button in the toolbar its own real identity.
    ImGui::PushID(static_cast<int>(icon));
    ImGui::BeginDisabled(!enabled);
    ImVec2 cursor = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton("##icon", ImVec2(size, size));
    bool hovered = ImGui::IsItemHovered();
    ImGui::EndDisabled();
    ImGui::PopID();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGuiStyle& style = ImGui::GetStyle();
    ImU32 bgCol = active ? ImGui::GetColorU32(ImGuiCol_ButtonActive)
                          : hovered ? ImGui::GetColorU32(ImGuiCol_ButtonHovered) : ImGui::GetColorU32(ImGuiCol_Button);
    dl->AddRectFilled(cursor, ImVec2(cursor.x + size, cursor.y + size), bgCol, style.FrameRounding);

    ImVec4 iconColorVec = enabled ? ImGui::GetStyleColorVec4(ImGuiCol_Text) : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    ImU32 iconCol = ImGui::GetColorU32(iconColorVec);
    ImVec2 center(cursor.x + size * 0.5f, cursor.y + size * 0.5f);

    switch (icon) {
        case Id::Open: drawOpen(dl, center, size, iconCol); break;
        case Id::Save: drawSave(dl, center, size, iconCol); break;
        case Id::Undo: drawCurvedArrow(dl, center, size, iconCol, false); break;
        case Id::Redo: drawCurvedArrow(dl, center, size, iconCol, true); break;
        case Id::Move: drawMove(dl, center, size, iconCol); break;
        case Id::Rotate: drawRotate(dl, center, size, iconCol); break;
        case Id::FrameAll: drawFrameAll(dl, center, size, iconCol); break;
        case Id::Grid: drawGrid(dl, center, size, iconCol); break;
        case Id::Geometry: drawGeometry(dl, center, size, iconCol); break;
        case Id::Speed: drawSpeed(dl, center, size, iconCol); break;
        case Id::GizmoObject: drawGizmoObject(dl, center, size, iconCol); break;
        case Id::GizmoStart: drawGizmoEndpoint(dl, center, size, iconCol, true); break;
        case Id::GizmoEnd: drawGizmoEndpoint(dl, center, size, iconCol, false); break;
        case Id::GizmoWhole: drawGizmoWhole(dl, center, size, iconCol); break;
        case Id::ColorObject: drawColorObject(dl, center, size, iconCol); break;
        case Id::ColorType: drawColorType(dl, center, size, iconCol); break;
        case Id::ColorLayer: drawColorLayer(dl, center, size, iconCol); break;
        case Id::ColorGroup: drawColorGroup(dl, center, size, iconCol); break;
        case Id::ColorSpeed: drawSpeed(dl, center, size, iconCol); break;
        case Id::ColorSequence: drawColorSequence(dl, center, size, iconCol); break;
    }

    if (tooltip && hovered) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(tooltip);
        ImGui::EndTooltip();
    }
    return clicked && enabled;
}

} // namespace Icons
