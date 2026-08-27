#include "ui/EditorUI.h"
#include "editor/BedConform.h"
#include "editor/CellTemplate.h"
#include "editor/InterleavePrint.h"
#include "editor/LayerZOffset.h"
#include "editor/MirrorObject.h"
#include "editor/ObjectLinking.h"
#include "editor/PathSplit.h"
#include "editor/RotatePaths.h"
#include "editor/Selection.h"
#include "editor/SpeedEditing.h"
#include "editor/SrcExporter.h"
#include "editor/Visibility.h"
#include "ui/Icons.h"

#include <imgui.h>
#include <algorithm>
#include <cstdio>

namespace {

// Reads Shift/Ctrl off ImGui's IO to decide how a selection action combines
// with the existing selection -- click=Replace, shift-click=Add,
// ctrl-click=Subtract. Matches the original's selection semantics
// (docs/PLAN.md task 7).
SelectionCompose currentSelectionCompose() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl) return SelectionCompose::Subtract;
    if (io.KeyShift) return SelectionCompose::Add;
    return SelectionCompose::Replace;
}

const char* colorModeLabel(ColorMode mode) {
    switch (mode) {
        case ColorMode::Object: return "Object";
        case ColorMode::Type: return "Type (print/travel)";
        case ColorMode::Layer: return "Layer";
        case ColorMode::Group: return "Selection group";
        case ColorMode::Speed: return "Speed";
        case ColorMode::Sequence: return "Print order (blue=first, red=last)";
    }
    return "?";
}

} // namespace

void EditorUI::sectionLabel(const char* text) {
    if (boldFont_) ImGui::PushFont(boldFont_);
    ImGui::TextUnformatted(text);
    if (boldFont_) ImGui::PopFont();
}

void EditorUI::draw(Scene& scene, ColorMode& colorMode, Camera& camera, RenderSettings& renderSettings,
                     BedSettings& bedSettings, LightingSettings& lightingSettings, BedHeightmap& bedHeightmap,
                     UndoStack& undoStack, size_t renderedPrimitiveCount, bool& sceneDirty, bool& selectionDirty, bool& bedDirty,
                     GizmoInteractionMode gizmoMode) {
    drawMenuBar(scene, undoStack, sceneDirty);
    drawToolbar(scene, camera, renderSettings, undoStack, sceneDirty, gizmoMode);
    drawStatusBar();
    drawExportDialog(scene); // must run even if panels are hidden -- it's a decision the user has to make

    // Panels collapsed: skip the dockspace and every window entirely,
    // leaving an unobstructed view of the viewport. The menu bar, toolbar,
    // and status bar stay visible either way -- the toggle button lives
    // in the menu bar, so it's always reachable to bring the panels back.
    if (!panelsVisible_) return;

    SceneObject* active = scene.activeObject();

    // A full-viewport passthrough dockspace: draws no chrome of its own,
    // just the invisible root every real window below docks into. The
    // 3D scene renders through the middle untouched (PassthruCentralNode)
    // wherever no window currently covers it.
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGuiID dockspaceId = ImGui::GetID("MainDockSpace");
    // DockSpaceOverViewport sizes/positions its own invisible host window
    // internally -- an explicit SetNextWindowPos/Size call here was
    // redundant at best, and a real suspect for the viewport vanishing
    // entirely (reported from real use) if it fought with that internal
    // sizing rather than just being overridden by it.
    // No pre-built default dock layout: an earlier DockBuilder-based
    // attempt at one reliably ate the entire passthrough center (the 3D
    // viewport disappeared completely -- reported from real use, and
    // reproduced/confirmed by disabling it). Root cause not fully
    // isolated within DockBuilder's split/dock-window API; rather than
    // ship a fragile fix for internals this session can't interactively
    // verify, windows just open as normal floating windows on first run
    // -- fully dockable by dragging them onto the viewport edges/each
    // other by hand, which is the ACTUAL supported way to arrange a
    // Dear ImGui docking layout regardless.
    ImGui::DockSpaceOverViewport(dockspaceId, viewport, ImGuiDockNodeFlags_PassthruCentralNode);

    // Seven dockable "big button" windows (see drawToolbar()'s launcher
    // row) instead of the old two tabbed catch-all windows -- each one
    // reachable, movable, and closable independently, per real request
    // ("the other tool should be big buttons that can be pressed and the
    // window will appear... everything is dockable").
    if (windowObjectLayersOpen_ && ImGui::Begin("Object & Layers", &windowObjectLayersOpen_)) {
        drawObjectListPanel(scene, undoStack, sceneDirty);
        if (active) {
            drawCellTemplatePanel(scene, *active, bedSettings, undoStack, sceneDirty);
            drawCommentPanel(*active);
            drawTransformPanel(scene, *active, undoStack, sceneDirty);
            drawLayerTablePanel(scene, *active, undoStack, sceneDirty, selectionDirty);
            drawSelectionGroupPanel(scene, *active, undoStack, sceneDirty, selectionDirty);
        } else {
            ImGui::TextDisabled("No object loaded. File > Open to load a .src file.");
        }
    }
    if (windowObjectLayersOpen_) ImGui::End();

    if (windowBedOpen_ && ImGui::Begin("Bed", &windowBedOpen_)) {
        drawBedPanel(bedSettings, lightingSettings, bedHeightmap, active, bedDirty);
    }
    if (windowBedOpen_) ImGui::End();

    if (windowAdvancedSpeedOpen_ && ImGui::Begin("Advanced Speed", &windowAdvancedSpeedOpen_)) {
        if (active) drawSpeedPanel(scene, *active, undoStack, sceneDirty);
        else ImGui::TextDisabled("No object loaded.");
    }
    if (windowAdvancedSpeedOpen_) ImGui::End();

    if (windowMirrorLinkOpen_ && ImGui::Begin("Mirror & Link", &windowMirrorLinkOpen_)) {
        drawMultiPartPanel(scene, undoStack, sceneDirty);
    }
    if (windowMirrorLinkOpen_) ImGui::End();

    if (windowGeometryOpen_ && ImGui::Begin("Geometry", &windowGeometryOpen_)) {
        drawViewPanel(camera, renderSettings, sceneDirty);
        ImGui::Separator();
        drawColorModePanel(colorMode, sceneDirty);
        if (active) {
            ImGui::Separator();
            drawBedConformPanel(scene, *active, bedHeightmap, bedSettings, undoStack, sceneDirty);
        }
        ImGui::Separator();
        drawStatsPanel(scene, renderSettings.mode, renderedPrimitiveCount);
    }
    if (windowGeometryOpen_) ImGui::End();

    if (windowLightsPreviewOpen_ && ImGui::Begin("Lights & Preview", &windowLightsPreviewOpen_)) {
        drawLightingPanel(lightingSettings);
    }
    if (windowLightsPreviewOpen_) ImGui::End();

    if (windowAnimationOpen_ && ImGui::Begin("Animation", &windowAnimationOpen_)) {
        drawAnimationPanel();
    }
    if (windowAnimationOpen_) ImGui::End();
}


// Icon row (Open/Save/Undo/Redo/Move/Rotate/FrameAll/Grid/Geometry/Speed)
// plus a second row of big launcher buttons that toggle each dockable
// window from drawToolbar()'s call site above. A thin, fixed,
// non-dockable strip pinned under the menu bar -- same positioning
// technique drawStatusBar() already uses at the bottom of the screen.
void EditorUI::drawToolbar(Scene& scene, Camera& camera, RenderSettings& renderSettings, UndoStack& undoStack,
                            bool& sceneDirty, GizmoInteractionMode gizmoMode) {
    (void)camera;
    ImGuiIO& io = ImGui::GetIO();
    float menuBarHeight = ImGui::GetFrameHeight();
    const float kToolbarHeight = 78.0f;
    ImGui::SetNextWindowPos(ImVec2(0.0f, menuBarHeight));
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, kToolbarHeight));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                              ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                              ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoNav;
    ImGui::Begin("##toolbar", nullptr, flags);

    const float kIconSize = 30.0f;
    bool hasActive = scene.activeObject() != nullptr;

    if (Icons::IconButton(Icons::Id::Open, kIconSize, false, true, "Open SRC / G-code...")) openFileRequested_ = true;
    ImGui::SameLine();
    if (Icons::IconButton(Icons::Id::Save, kIconSize, false, hasActive, "Save SRC As...")) {
        exportDialogOpen_ = true;
        exportCheckOutcomes_.clear();
    }
    ImGui::SameLine();
    ImGui::Dummy(ImVec2(12.0f, 0.0f));
    ImGui::SameLine();
    if (Icons::IconButton(Icons::Id::Undo, kIconSize, false, undoStack.canUndo(), "Undo (Ctrl+Z)")) {
        undoStack.undo(scene);
        sceneDirty = true;
    }
    ImGui::SameLine();
    if (Icons::IconButton(Icons::Id::Redo, kIconSize, false, undoStack.canRedo(), "Redo (Ctrl+Y)")) {
        undoStack.redo(scene);
        sceneDirty = true;
    }
    ImGui::SameLine();
    ImGui::Dummy(ImVec2(12.0f, 0.0f));
    ImGui::SameLine();
    if (Icons::IconButton(Icons::Id::Move, kIconSize, gizmoMode == GizmoInteractionMode::Move, true, "Move tool"))
        moveToolRequested_ = true;
    ImGui::SameLine();
    if (Icons::IconButton(Icons::Id::Rotate, kIconSize, gizmoMode == GizmoInteractionMode::Rotate, true, "Rotate tool (R)"))
        rotateToolRequested_ = true;
    ImGui::SameLine();
    ImGui::Dummy(ImVec2(12.0f, 0.0f));
    ImGui::SameLine();
    if (Icons::IconButton(Icons::Id::FrameAll, kIconSize, false, true, "Frame all (F)")) frameAllRequested_ = true;
    ImGui::SameLine();
    if (Icons::IconButton(Icons::Id::Grid, kIconSize, false, true, "Toggle grid (G)")) toggleGridRequested_ = true;
    ImGui::SameLine();
    bool geometryModeOn = renderSettings.mode == RenderMode::Geometry;
    if (Icons::IconButton(Icons::Id::Geometry, kIconSize, geometryModeOn, true, "Toggle Lines / Geometry render mode")) {
        renderSettings.mode = geometryModeOn ? RenderMode::Lines : RenderMode::Geometry;
        sceneDirty = true;
    }
    ImGui::SameLine();
    ImGui::Dummy(ImVec2(12.0f, 0.0f));
    ImGui::SameLine();
    if (Icons::IconButton(Icons::Id::Speed, kIconSize, windowAdvancedSpeedOpen_, true, "Advanced speed options"))
        windowAdvancedSpeedOpen_ = !windowAdvancedSpeedOpen_;

    // Second row: big launcher buttons, one per dockable window.
    auto launcher = [&](const char* label, bool* openFlag) {
        if (ImGui::Button(label, ImVec2(0.0f, 30.0f))) *openFlag = !*openFlag;
        ImGui::SameLine();
    };
    launcher("Object & Layers", &windowObjectLayersOpen_);
    launcher("Bed", &windowBedOpen_);
    launcher("Advanced Speed", &windowAdvancedSpeedOpen_);
    launcher("Mirror & Link", &windowMirrorLinkOpen_);
    launcher("Geometry", &windowGeometryOpen_);
    launcher("Lights & Preview", &windowLightsPreviewOpen_);
    if (ImGui::Button("Animation", ImVec2(0.0f, 30.0f))) windowAnimationOpen_ = !windowAnimationOpen_;

    ImGui::End();
}

void EditorUI::drawMenuBar(Scene& scene, UndoStack& undoStack, bool& sceneDirty) {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open SRC / G-code...")) {
                openFileRequested_ = true;
            }
            bool hasActive = (scene.activeObject() != nullptr);
            if (ImGui::MenuItem("Save SRC As...", nullptr, false, hasActive)) {
                exportDialogOpen_ = true;
                exportCheckOutcomes_.clear(); // fresh dialog -- no stale results from a previous object/edit
            }
            ImGui::Separator();
            // A .src is a robot program and can only hold what the robot
            // needs. Selections, groups, bed + heightmap, the measured
            // safe point, object links, per-object colours -- none of
            // that has anywhere to live in a .src, so it dies with the
            // app unless it goes in a project file.
            if (ImGui::MenuItem("Open Project...")) loadProjectRequested_ = true;
            if (ImGui::MenuItem("Save Project As...")) saveProjectRequested_ = true;
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, undoStack.canUndo())) {
                undoStack.undo(scene);
                sceneDirty = true;
            }
            if (ImGui::MenuItem("Redo", "Ctrl+Y", false, undoStack.canRedo())) {
                undoStack.redo(scene);
                sceneDirty = true;
            }
            ImGui::EndMenu();
        }
        // Panel collapse toggle: lives directly in the menu bar (not its
        // own floating window) so it's always visible and reachable
        // regardless of panelsVisible_'s current state -- clicking it
        // hides/shows the Editor and Bed windows for an unobstructed view
        // of the viewport, click again to bring them back.
        if (ImGui::SmallButton(panelsVisible_ ? "Hide panels" : "Show panels")) {
            panelsVisible_ = !panelsVisible_;
        }
        ImGui::EndMainMenuBar();
    }
}

void EditorUI::drawStatusBar() {
    // A real bottom-of-screen status strip, not a floating window: fixed
    // to the full display width at the bottom, no decoration, no input
    // capture -- so it never steals a click meant for the viewport
    // underneath it.
    const float kHeight = 28.0f;
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0.0f, io.DisplaySize.y - kHeight));
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, kHeight));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                              ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                              ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 5.0f));
    if (ImGui::Begin("##statusbar", nullptr, flags)) {
        if (hoverInfo_.valid) {
            if (hoverInfo_.isTravel) {
                ImGui::Text("%s   path %d   TRAVEL   speed %.4f",
                            hoverInfo_.objectName.c_str(), hoverInfo_.pathNumber, hoverInfo_.speed);
            } else {
                ImGui::Text("%s   path %d   layer %d   speed %.4f",
                            hoverInfo_.objectName.c_str(), hoverInfo_.pathNumber,
                            hoverInfo_.layer, hoverInfo_.speed);
            }
        } else {
            ImGui::TextDisabled("Hover a path to see its layer and speed.   Tab: hide/show panels");
        }

        // FPS in the corner. ImGui already tracks a smoothed framerate
        // internally (averaged over the last ~120 frames) -- no need for
        // our own timer.
        char fpsText[32];
        std::snprintf(fpsText, sizeof(fpsText), "%.0f FPS", ImGui::GetIO().Framerate);
        ImVec2 fpsSize = ImGui::CalcTextSize(fpsText);
        ImGui::SameLine(ImGui::GetWindowWidth() - fpsSize.x - 14.0f);
        ImGui::TextDisabled("%s", fpsText);
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

// Runs one check (or all of them) against a fresh compile of `object`
// under the dialog's current rounding option, storing results into
// exportCheckOutcomes_ so both the per-check row and the combined report
// below can read them back. Re-compiles on every click rather than
// caching -- exports are cheap (patch a copy of the source lines) and
// this guarantees the result always reflects whatever the object/options
// currently say, never a stale compile from before an edit.
void EditorUI::drawExportDialog(Scene& scene) {
    if (!exportDialogOpen_) return;

    SceneObject* active = scene.activeObject();
    ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_Appearing);
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::Begin("Export SRC", &exportDialogOpen_, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }

    if (!active) {
        ImGui::TextDisabled("No object loaded.");
        ImGui::End();
        return;
    }

    auto checks = exportValidationChecks();
    if (exportCheckOutcomes_.size() != checks.size()) exportCheckOutcomes_.assign(checks.size(), CheckOutcome{});

    ImGui::Text("Exporting: %s", active->name.c_str());
    ImGui::Separator();

    sectionLabel("Options");
    if (ImGui::Checkbox("Round exported speeds to 4 decimal places", &exportRoundSpeeds_)) {
        // The option changed what a re-run would report -- stale results
        // from before the toggle would be actively misleading (a fresh
        // rounding gap that "Speed match" hasn't re-checked yet), so clear
        // them rather than let them keep showing an outcome for the OLD
        // setting.
        exportCheckOutcomes_.assign(checks.size(), CheckOutcome{});
    }
    ImGui::TextDisabled("Off: $VEL.CP written with 6 decimals (0.060000). On: 4 (0.0600).");

    // A rounding gap up to half of the 4th-decimal step (0.00005) between
    // the intended and exported speed is EXPECTED once rounding is on --
    // widen the speed-match tolerance to match, or it would flag every
    // rounded speed as a false mismatch.
    double speedTolerance = exportRoundSpeeds_ ? 6e-5 : 1e-9;

    auto runCheck = [&](size_t i) {
        ExportOptions options;
        options.roundSpeedsTo4Decimals = exportRoundSpeeds_;
        ExportResult exportResult;
        std::vector<std::string> compiled = buildExportedLines(*active, exportResult, options);
        ValidationReport single;
        checks[i].run(*active, compiled, speedTolerance, single);
        exportCheckOutcomes_[i].run = true;
        exportCheckOutcomes_[i].issues = single.issues;
    };

    ImGui::Spacing();
    sectionLabel("Checks");
    ImGui::TextWrapped("Run one at a time to see its own result, or run everything at once.");

    for (size_t i = 0; i < checks.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::SmallButton("Run")) runCheck(i);
        ImGui::SameLine();
        const CheckOutcome& outcome = exportCheckOutcomes_[i];
        if (!outcome.run) {
            ImGui::TextDisabled("%s -- not run yet", checks[i].name.c_str());
        } else {
            int critical = 0, warning = 0;
            for (const auto& issue : outcome.issues) {
                if (issue.severity == ValidationSeverity::Critical) ++critical; else ++warning;
            }
            if (critical > 0) {
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.40f, 1.0f), "%s -- %d critical",
                                    checks[i].name.c_str(), critical);
            } else if (warning > 0) {
                ImGui::TextColored(ImVec4(1.0f, 0.80f, 0.40f, 1.0f), "%s -- %d warning(s)",
                                    checks[i].name.c_str(), warning);
            } else {
                ImGui::TextColored(ImVec4(0.45f, 0.90f, 0.50f, 1.0f), "%s -- OK", checks[i].name.c_str());
            }
        }
        ImGui::PopID();
    }

    ImGui::Spacing();
    if (ImGui::Button("Run all tests")) {
        for (size_t i = 0; i < checks.size(); ++i) runCheck(i);
    }

    ImGui::Spacing();
    sectionLabel("Report");
    bool allRun = true;
    int totalCritical = 0, totalWarning = 0;
    for (const auto& outcome : exportCheckOutcomes_) {
        if (!outcome.run) { allRun = false; continue; }
        for (const auto& issue : outcome.issues) {
            if (issue.severity == ValidationSeverity::Critical) ++totalCritical; else ++totalWarning;
        }
    }
    if (!allRun) ImGui::TextDisabled("Not every check has been run yet.");

    ImGui::BeginChild("##exportReportList", ImVec2(0, 180), ImGuiChildFlags_Borders);
    for (size_t i = 0; i < checks.size(); ++i) {
        if (!exportCheckOutcomes_[i].run) continue;
        for (const auto& issue : exportCheckOutcomes_[i].issues) {
            bool critical = (issue.severity == ValidationSeverity::Critical);
            ImGui::PushStyleColor(ImGuiCol_Text, critical ? ImVec4(1.0f, 0.45f, 0.40f, 1.0f)
                                                           : ImVec4(1.0f, 0.80f, 0.40f, 1.0f));
            ImGui::TextWrapped("[%s] %s: %s", checks[i].name.c_str(),
                                critical ? "CRITICAL" : "WARNING", issue.message.c_str());
            ImGui::PopStyleColor();
        }
    }
    if (totalCritical == 0 && totalWarning == 0 && allRun) {
        ImGui::TextColored(ImVec4(0.45f, 0.90f, 0.50f, 1.0f), "All checks passed.");
    }
    ImGui::EndChild();

    ImGui::Spacing();
    // Critical issues block saving outright -- matches the structural-
    // vs-warning severity split every other check in this codebase uses
    // (a critical issue means the file would very likely fail to load or
    // run on the robot). Only checks that have actually been RUN count
    // here; an unrun check can't be known to be clean, so it doesn't
    // block, but it also can't vouch for the file -- see the "not every
    // check has been run yet" notice above.
    ImGui::BeginDisabled(totalCritical > 0);
    if (ImGui::Button("Save SRC...", ImVec2(140, 0))) {
        exportSaveRequested_ = true;
        exportDialogOpen_ = false;
    }
    ImGui::EndDisabled();
    if (totalCritical > 0) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.40f, 1.0f), "Fix critical issues first.");
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100, 0))) exportDialogOpen_ = false;

    ImGui::End();
}

void EditorUI::drawAnimationPanel() {
    AnimationSettings& s = animationSettings_;

    sectionLabel("Print simulation");
    ImGui::TextWrapped("Simulates printing the active object in real time -- the bead geometry reveals itself "
                        "as the head moves, synced to each path's actual speed (not just a marker over "
                        "already-drawn geometry).");

    ImGui::Checkbox("Include print paths", &s.includePrint);
    ImGui::SameLine();
    ImGui::Checkbox("Include travels", &s.includeTravel);

    ImGui::DragFloat("Max segment length (mm)", &s.maxSegmentLengthMm, 0.1f, 0.5f, 200.0f, "%.1f");
    ImGui::TextDisabled("Long straight paths split into pieces this long or shorter, so printing reveals "
                         "gradually instead of the whole path popping in at once.");
    ImGui::DragFloat("Fallback speed (m/s)", &s.fallbackSpeedMps, 0.001f, 0.001f, 5.0f, "%.3f");
    ImGui::TextDisabled("Used for any path with no recorded speed, so the simulation never stalls.");

    if (ImGui::Button(animBuilt_ ? "Rebuild" : "Build simulation")) animationBuildRequested_ = true;
    ImGui::SameLine();
    ImGui::TextDisabled(animBuilt_ ? "Ready." : "Not built yet -- click to simulate the active object.");

    // While a simulation is built, it REPLACES the normal 3D view of the
    // object entirely (see main.cpp's draw dispatch) -- this is the only
    // way back. Stop alone doesn't do it (Stop just pauses/rewinds
    // playback, so the simulation stays built and the normal view stays
    // hidden), which is exactly the "I can't get back to my file" bug
    // this button fixes.
    ImGui::BeginDisabled(!animBuilt_);
    if (ImGui::Button("Back to editor view")) animationExitRequested_ = true;
    ImGui::EndDisabled();

    ImGui::Spacing();
    sectionLabel("Print head");
    ImGui::Checkbox("Show print head", &s.showHead);
    ImGui::DragFloat("Head width (mm)", &s.headWidthMm, 1.0f, 1.0f, 2000.0f, "%.0f");
    ImGui::DragFloat("Head depth (mm)", &s.headDepthMm, 1.0f, 1.0f, 2000.0f, "%.0f");
    ImGui::DragFloat("Head height (mm)", &s.headHeightMm, 1.0f, 1.0f, 2000.0f, "%.0f");
    ImGui::DragFloat("Nozzle length (mm)", &s.nozzleLengthMm, 1.0f, 1.0f, 1000.0f, "%.0f");
    ImGui::DragFloat("Nozzle width (mm)", &s.nozzleWidthMm, 1.0f, 1.0f, 500.0f, "%.0f");

    ImGui::Spacing();
    sectionLabel("Playback");
    ImGui::BeginDisabled(!animBuilt_);
    ImGui::SliderFloat("Speed", &s.speedMultiplier, 0.1f, 50.0f, "%.2fx (1x = real time)");

    if (ImGui::Button(animRunning_ ? "Pause" : "Play")) {
        if (animRunning_) animationPauseRequested_ = true; else animationPlayRequested_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop")) animationStopRequested_ = true;

    // Full panel width and noticeably taller than a default slider --
    // this is the primary scrub control, not a minor setting, so it
    // should read as the biggest thing in the tab and grow with the
    // panel instead of a small fixed-width bar.
    float sliderTime = static_cast<float>(animCurrentTime_);
    float sliderMax = static_cast<float>(std::max(animTotalTime_, 0.0001));
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 10.0f));
    if (ImGui::SliderFloat("##animTimeline", &sliderTime, 0.0f, sliderMax, "")) {
        animationScrubbed_ = true;
        animationScrubTimeSeconds_ = static_cast<double>(sliderTime);
    }
    ImGui::PopStyleVar();

    auto formatTime = [](double seconds) {
        int totalSeconds = static_cast<int>(seconds + 0.5);
        int minutes = totalSeconds / 60, secs = totalSeconds % 60;
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%d:%02d", minutes, secs);
        return std::string(buffer);
    };
    double progress = (animTotalTime_ > 1e-9) ? (animCurrentTime_ / animTotalTime_) : 0.0;
    ImGui::Text("%s / %s  (%.1f%%)%s", formatTime(animCurrentTime_).c_str(), formatTime(animTotalTime_).c_str(),
                progress * 100.0, animFinished_ ? "  -- Completed" : "");
    ImGui::EndDisabled();
}

void EditorUI::drawViewPanel(Camera& camera, RenderSettings& renderSettings, bool& dirty) {
    sectionLabel("View");

    bool isPerspective = (camera.projection() == Camera::Projection::Perspective);
    if (ImGui::RadioButton("Perspective", isPerspective)) camera.setProjection(Camera::Projection::Perspective);
    ImGui::SameLine();
    if (ImGui::RadioButton("Orthographic", !isPerspective)) camera.setProjection(Camera::Projection::Orthographic);

    if (ImGui::Button("Top")) camera.setPreset(Camera::Preset::Top);
    ImGui::SameLine();
    if (ImGui::Button("Front")) camera.setPreset(Camera::Preset::Front);
    ImGui::SameLine();
    if (ImGui::Button("Right")) camera.setPreset(Camera::Preset::Right);
    ImGui::SameLine();
    if (ImGui::Button("Iso")) camera.setPreset(Camera::Preset::Iso);

    ImGui::TextDisabled("Alt+drag: LMB orbit / MMB pan / RMB zoom. Scroll also zooms.");
    ImGui::TextDisabled("Plain click: select path. Plain drag: marquee-select.");

    ImGui::Spacing();
    sectionLabel("Display");
    // Pure display filters -- hiding a category doesn't delete it or
    // exclude it from export, it just declutters the viewport.
    if (ImGui::Checkbox("Paths", &renderSettings.showPrintPaths)) dirty = true;
    ImGui::SameLine();
    if (ImGui::Checkbox("Travels", &renderSettings.showTravels)) dirty = true;
    ImGui::SameLine();
    ImGui::Checkbox("Start point", &renderSettings.showStartPoint); // draw-time only, no rebuild needed
    if (ImGui::Checkbox("Vertices", &renderSettings.showVertices)) dirty = true;
    if (renderSettings.showVertices) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110.0f);
        ImGui::DragFloat("Size##vtx", &renderSettings.vertexSizePixels, 0.2f, 1.0f, 20.0f, "%.0f px");
    }
    ImGui::TextDisabled("Start point = the joint-space PTP the robot moves to before printing.");
    ImGui::TextDisabled("Vertices = the actual endpoints of each motion command.");

    ImGui::Spacing();
    sectionLabel("Render mode");
    bool isLines = (renderSettings.mode == RenderMode::Lines);
    if (ImGui::RadioButton("Lines", isLines)) {
        if (!isLines) { renderSettings.mode = RenderMode::Lines; dirty = true; }
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Geometry (bead)", !isLines)) {
        if (isLines) { renderSettings.mode = RenderMode::Geometry; dirty = true; }
    }

    if (renderSettings.mode == RenderMode::Geometry) {
        // DragFloat instead of SliderFloat: click-drag the number itself to
        // scrub the value (also still double-click/ctrl-click to type an
        // exact number), rather than needing to hit a thin slider track.
        if (ImGui::DragFloat("Bead width (mm)", &renderSettings.beadWidthMm, 0.1f, 0.5f, 50.0f, "%.1f")) dirty = true;
        if (ImGui::DragFloat("Bead height (mm)", &renderSettings.beadHeightMm, 0.1f, 0.5f, 50.0f, "%.1f")) dirty = true;
        ImGui::TextDisabled("Print paths render as mitered solid bead tubes; travel paths stay as thin lines.");
        // Rendering-only toggle -- no rebuild needed, just checked at draw time.
        ImGui::Checkbox("Backface culling (hide inside of tubes)", &renderSettings.backfaceCulling);

        // Selection style is a pure draw-time choice too -- no rebuild
        // needed, GeometryRenderer already builds both the outline mesh
        // and the per-vertex selected flag every rebuild regardless of
        // which style is currently active, so switching is instant.
        static const char* kSelectionStyleNames[] = {
            "Outline", "Pulse (glow selected, dim rest)", "Stripes (moving hazard tape)", "Wireframe cage"
        };
        int styleIndex = static_cast<int>(renderSettings.selectionStyle);
        if (ImGui::Combo("Selection style", &styleIndex, kSelectionStyleNames, 4)) {
            renderSettings.selectionStyle = static_cast<SelectionStyle>(styleIndex);
        }
    }

    ImGui::Spacing();
    // Picking-only toggle -- affects the next click, nothing to rebuild.
    ImGui::Checkbox("Select backfacing/hidden geometry", &renderSettings.selectBackfacing);
    ImGui::TextDisabled("Off: clicking prefers the path nearest the camera. On: prefers whichever is closest on screen.");

    ImGui::Spacing();
    sectionLabel("Move gizmo");
    GizmoTargetMode& gm = renderSettings.gizmoMode;
    if (ImGui::RadioButton("Object", gm == GizmoTargetMode::Object)) gm = GizmoTargetMode::Object;
    ImGui::SameLine();
    if (ImGui::RadioButton("Start", gm == GizmoTargetMode::Start)) gm = GizmoTargetMode::Start;
    ImGui::SameLine();
    if (ImGui::RadioButton("End", gm == GizmoTargetMode::End)) gm = GizmoTargetMode::End;
    ImGui::SameLine();
    if (ImGui::RadioButton("Whole", gm == GizmoTargetMode::Whole)) gm = GizmoTargetMode::Whole;
    ImGui::TextDisabled("Object moves the whole object. Start/End/Whole edit the CURRENT PATH SELECTION directly");
    ImGui::TextDisabled("(with no selection, these fall back to Object mode). Start/End can break connectivity with");
    ImGui::TextDisabled("an unselected neighbor; Whole translates each selected path rigidly, so it never does.");
    ImGui::TextDisabled("Press R in the viewport to switch the gizmo between Move (arrows) and Rotate (ring) --");
    ImGui::TextDisabled("rotation always spins around the same Object/Start/End/Whole target set above.");
}

// Extracted out of drawBedPanel (see its own "Lights & Preview" dockable
// window) -- self-contained, touches only `lighting`. Affects only
// Geometry-mode shading (a per-frame shader uniform, not baked into any
// mesh) -- no dirty flag needed, the next frame's draw call just picks
// up the new values directly.
void EditorUI::drawLightingPanel(LightingSettings& lighting) {
    sectionLabel("Environment / Lighting");
    ImGui::TextDisabled("Affects Geometry view mode shading only.");

    if (ImGui::Button("Three-point lighting preset")) {
        // Classic film/photography three-point setup: a bright key light
        // from one upper-front side, a dimmer fill from the other side to
        // soften the key's shadows without erasing them, and a subtle rim
        // light from behind to separate the geometry from the background.
        lighting.lights = {
            Light{glm::vec3(0.5f, -0.6f, 0.8f), glm::vec3(1.0f, 1.0f, 1.0f), true},   // key
            Light{glm::vec3(-0.6f, -0.3f, 0.4f), glm::vec3(0.5f, 0.55f, 0.6f), true}, // fill
            Light{glm::vec3(0.0f, 0.9f, 0.3f), glm::vec3(0.4f, 0.35f, 0.3f), true},   // rim/back
        };
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(key + fill + rim)");

    int lightToRemove = -1;
    for (size_t i = 0; i < lighting.lights.size(); ++i) {
        Light& light = lighting.lights[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::Separator();
        ImGui::Checkbox("Enabled", &light.enabled);
        ImGui::SameLine();
        ImGui::Text("Light %d", static_cast<int>(i + 1));
        ImGui::SameLine(ImGui::GetWindowWidth() - 60.0f);
        bool canRemove = lighting.lights.size() > 1;
        ImGui::BeginDisabled(!canRemove);
        if (ImGui::SmallButton("Remove")) lightToRemove = static_cast<int>(i);
        ImGui::EndDisabled();

        float dir[3] = {light.direction.x, light.direction.y, light.direction.z};
        if (ImGui::SliderFloat3("Direction", dir, -1.0f, 1.0f)) {
            light.direction = glm::vec3(dir[0], dir[1], dir[2]);
        }
        float color[3] = {light.color.r, light.color.g, light.color.b};
        if (ImGui::ColorEdit3("Color", color)) {
            light.color = glm::vec3(color[0], color[1], color[2]);
        }
        ImGui::PopID();
    }
    if (lightToRemove >= 0) {
        lighting.lights.erase(lighting.lights.begin() + lightToRemove);
    }

    ImGui::Spacing();
    ImGui::BeginDisabled(static_cast<int>(lighting.lights.size()) >= LightingSettings::kMaxLights);
    if (ImGui::Button("Add Light")) {
        lighting.lights.push_back(Light{});
    }
    ImGui::EndDisabled();
}

void EditorUI::drawBedPanel(BedSettings& bed, LightingSettings& lighting, BedHeightmap& heightmap,
                             SceneObject* activeObject, bool& bedDirty) {
    (void)lighting; // lighting controls now live in their own "Lights & Preview" window (drawLightingPanel)
    sectionLabel("Bed size");
    if (ImGui::InputFloat("Width (mm)", &bed.widthMm, 10.0f, 100.0f, "%.0f")) { bed.widthMm = std::max(bed.widthMm, 10.0f); bedDirty = true; }
    if (ImGui::InputFloat("Depth (mm)", &bed.depthMm, 10.0f, 100.0f, "%.0f")) { bed.depthMm = std::max(bed.depthMm, 10.0f); bedDirty = true; }

    ImGui::Spacing();
    sectionLabel("Bed position (movement)");
    if (ImGui::InputFloat("Origin X (mm)", &bed.originXMm, 10.0f, 100.0f, "%.1f")) bedDirty = true;
    if (ImGui::InputFloat("Origin Y (mm)", &bed.originYMm, 10.0f, 100.0f, "%.1f")) bedDirty = true;
    if (ImGui::InputFloat("Origin Z (mm)", &bed.originZMm, 10.0f, 100.0f, "%.1f")) bedDirty = true;

    ImGui::Spacing();
    sectionLabel("Grid");
    if (ImGui::Checkbox("Show grid", &bed.showGrid)) bedDirty = true;
    if (ImGui::InputFloat("Line spacing (mm)", &bed.gridSpacingMm, 5.0f, 50.0f, "%.0f")) {
        bed.gridSpacingMm = std::max(bed.gridSpacingMm, 1.0f);
        bedDirty = true;
    }
    ImGui::TextDisabled("Default: 100mm (10cm) per line.");

    if (ImGui::Button("Reset bed")) {
        bed = BedSettings{};
        bedDirty = true;
    }

    // Safe point: a CELL property, which is why it lives here with the
    // bed rather than on the part. The same robot goes to the same safe
    // pose for every job, so it's entered once and saved with the bed.
    ImGui::Spacing();
    sectionLabel("Robot safe point (start position)");
    ImGui::TextWrapped("The joint-space PTP the robot moves to before printing has no X/Y/Z in the "
                        "program -- read it off the pendant (Display > Actual Position > Cartesian, "
                        "with Tool 1 / Base 1 active) and enter it here.");

    float safeXYZ[3] = {bed.safePointXMm, bed.safePointYMm, bed.safePointZMm};
    if (ImGui::DragFloat3("Safe X/Y/Z (mm)", safeXYZ, 1.0f, 0.0f, 0.0f, "%.1f")) {
        bed.safePointXMm = safeXYZ[0];
        bed.safePointYMm = safeXYZ[1];
        bed.safePointZMm = safeXYZ[2];
        bed.safePointMeasured = true;
        bedDirty = true;
    }
    if (bed.safePointMeasured) {
        ImGui::TextColored(ImVec4(0.45f, 0.90f, 0.50f, 1.0f), "Measured -- marker shows the real position.");
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear##safepoint")) {
            bed.safePointMeasured = false;
            bedDirty = true;
        }
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.70f, 0.30f, 1.0f),
                            "Not measured -- marker falls back to the program's first point, which is NOT the safe pose.");
    }

    // Cell template: also a CELL property (safety interrupts and I/O
    // indices are specific to this robot, not to any one file), captured
    // once from a real known-good program and reused to "fix" an object
    // that has none of its own -- see editor/CellTemplate.h. Real-use
    // report: an interleaved 5-copy export had lost &ACCESS, the safety
    // interrupts, and the shutdown block entirely; a plain sliced .gcode
    // import will have NONE of that to begin with.
    ImGui::Spacing();
    sectionLabel("Cell template (header/footer fix)");
    ImGui::TextWrapped("The real header (safety interrupts, safe-pose PTP) and footer (retreat travel, "
                        "extruder/bed/cooling shutoff) a program needs to actually run. Capture it once "
                        "from a known-good file, then use it to fix an object missing its own -- a plain "
                        "sliced import, for instance.");
    if (bed.cellTemplate.captured) {
        ImGui::TextColored(ImVec4(0.45f, 0.90f, 0.50f, 1.0f), "Captured: %d header line(s), %d footer line(s).",
                            static_cast<int>(bed.cellTemplate.headerLines.size()),
                            static_cast<int>(bed.cellTemplate.footerLines.size()));
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear##celltemplate")) {
            bed.cellTemplate = CellTemplate{};
            bedDirty = true;
        }
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.70f, 0.30f, 1.0f), "Not captured yet.");
    }

    bool canCapture = activeObject && objectHasBoilerplate(*activeObject);
    ImGui::BeginDisabled(!canCapture);
    if (ImGui::Button("Capture from active object")) {
        if (auto tmpl = captureCellTemplate(*activeObject)) {
            bed.cellTemplate = *tmpl;
            bedDirty = true;
        }
    }
    ImGui::EndDisabled();
    if (!canCapture) {
        ImGui::TextDisabled(activeObject
                                 ? "Active object has no real header/footer of its own to capture."
                                 : "No object loaded.");
    }

    ImGui::Spacing();
    if (ImGui::Button("Save Bed...")) saveBedRequested_ = true;
    ImGui::SameLine();
    if (ImGui::Button("Load Bed...")) loadBedRequested_ = true;
    ImGui::TextDisabled("Bed file stores size, origin, grid, heightmap, the safe point, and the cell template.");

    // Bed heightmap: operator-entered elevation measurements across the
    // bed, visualized as a colored heatmap surface so warp is visible
    // before it ruins a print. Columns/rows are the source of truth here
    // (not a spacing value) -- the operator states the grid directly
    // ("10 columns, 5 rows"), and X/Y spacing is just bed size divided by
    // (cols-1)/(rows-1). bedDirty is reused for every edit here (columns,
    // rows, per-point value, visibility) -- main.cpp's bedDirty handler
    // already rebuilds BedHeightmapRenderer whenever it fires, so this
    // doesn't need its own separate dirty flag threaded through the whole
    // call chain.
    ImGui::Spacing();
    ImGui::Spacing();
    sectionLabel("Bed Heightmap");
    ImGui::TextWrapped("Enter measured bed elevation at each grid point to see where the bed is "
                        "elevated too much, as a heatmap. Point (0,0) is one bed corner, "
                        "(columns-1, rows-1) is the opposite corner.");

    if (ImGui::Checkbox("Show heatmap", &heightmap.visible)) bedDirty = true;

    int cols = heightmap.cols;
    int rows = heightmap.rows;
    bool colsChanged = ImGui::InputInt("Columns", &cols);
    bool rowsChanged = ImGui::InputInt("Rows", &rows);
    if (colsChanged || rowsChanged) {
        heightmap.resize(std::max(cols, 2), std::max(rows, 2));
        bedDirty = true;
    }

    if (ImGui::Button("Reset all to 0")) {
        std::fill(heightmap.elevationsMm.begin(), heightmap.elevationsMm.end(), 0.0f);
        bedDirty = true;
    }

    // Click-a-vertex paint tool -- an alternative to typing into the grid
    // table below: while active, the nearest heightmap vertex to the
    // cursor is highlighted live (white, see main.cpp's per-frame hover
    // tracking + BedHeightmapRenderer's highlightCol/Row), a plain click
    // raises it by Power, Alt+click lowers it. No separate Add/Remove
    // mode to toggle -- one modifier key, matching every other "click vs
    // alt-click" convention already in this app (camera orbit/pan/zoom).
    // No radius/falloff -- exactly the nearest vertex, nothing more.
    ImGui::Spacing();
    if (ImGui::Checkbox("Paint mode (click a vertex in the 3D view)", &heightmapPaintMode_)) {
        // Painting a surface you can't see isn't paintable -- force it
        // visible the moment paint mode turns on, rather than leaving the
        // operator clicking blind and concluding the tool does nothing.
        if (heightmapPaintMode_ && !heightmap.visible) {
            heightmap.visible = true;
            bedDirty = true;
        }
    }
    if (heightmapPaintMode_) {
        ImGui::TextDisabled("Click raises the nearest vertex (white highlight), Alt+click lowers it.");
        ImGui::TextDisabled("A click in the viewport selects paths as normal -- this intercepts it instead.");
        ImGui::DragFloat("Power (mm per click)", &heightmapPaintPowerMm_, 0.01f, 0.01f, 20.0f, "%.2f");
    }

    // Save/Load Bed already exist up in the "Bed size" section above, but
    // this panel is tall enough now (lighting + a 100+ field grid) that
    // scrolling back up just to save entered measurements is annoying --
    // duplicate the same two buttons here. saveBedRequested_/
    // loadBedRequested_ are the exact same request flags the top buttons
    // set; main.cpp's handling already saves/loads the heightmap alongside
    // the rest of the bed settings.
    if (ImGui::Button("Save Bed...##heightmap")) saveBedRequested_ = true;
    ImGui::SameLine();
    if (ImGui::Button("Load Bed...##heightmap")) loadBedRequested_ = true;

    if (heightmap.cols >= 2 && heightmap.rows >= 2) {
        ImGui::Text("%d x %d grid (%d points)", heightmap.cols, heightmap.rows, heightmap.cols * heightmap.rows);
        ImGui::TextDisabled("Drag a value to scrub it, or double-click/Ctrl+click to type an exact number.");

        // Scrollable child so a large grid doesn't blow out the rest of
        // the Bed panel.
        ImGui::BeginChild("heightmapGrid", ImVec2(0, 220), true, ImGuiWindowFlags_HorizontalScrollbar);
        if (ImGui::BeginTable("heightmapTable", heightmap.cols, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit)) {
            // Row 0 (the -Y edge) drawn FIRST. Counterintuitive but
            // correct: Camera's Top preset looks straight down -Z with its
            // "up" vector derived from the SAME orbit quaternion as every
            // other view (see Camera::viewMatrix/orientation) -- at that
            // preset's yaw/pitch, screen-up works out to world -Y, not
            // +Y. So the row that reads at the TOP of this table (and
            // should look like the top of the bed in Top view) is the one
            // at the smallest Y, i.e. row 0. Getting this backwards was a
            // real reported bug: an operator's entered values rendered
            // mirrored top-to-bottom versus where they expected them.
            for (int row = 0; row < heightmap.rows; ++row) {
                ImGui::TableNextRow();
                for (int col = 0; col < heightmap.cols; ++col) {
                    ImGui::TableNextColumn();
                    ImGui::PushID(row * heightmap.cols + col);
                    ImGui::SetNextItemWidth(56.0f);
                    if (ImGui::DragFloat("##v", &heightmap.at(col, row), 0.02f, -50.0f, 50.0f, "%.2f")) {
                        bedDirty = true;
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndTable();
        }
        ImGui::EndChild();
    } else {
        ImGui::TextDisabled("No grid yet -- click \"Resize grid to bed\".");
    }
}

void EditorUI::drawObjectListPanel(Scene& scene, UndoStack& undoStack, bool& dirty) {
    sectionLabel("Objects");
    if (ImGui::BeginTable("objects", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
        ImGui::TableSetupColumn("Visible", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Color", ImGuiTableColumnFlags_WidthFixed, 40.0f);
        ImGui::TableSetupColumn("Reorder", ImGuiTableColumnFlags_WidthFixed, 55.0f);
        ImGui::TableSetupColumn("Link->next", ImGuiTableColumnFlags_WidthFixed, 75.0f);
        ImGui::TableSetupColumn("Delete", ImGuiTableColumnFlags_WidthFixed, 55.0f);
        ImGui::TableHeadersRow();

        int objectIdPendingDelete = -1;

        for (size_t i = 0; i < scene.objects.size(); ++i) {
            SceneObject& object = scene.objects[i];
            ImGui::TableNextRow();
            ImGui::PushID(object.id);

            ImGui::TableNextColumn();
            bool visible = object.visible;
            if (ImGui::Checkbox("##visible", &visible)) {
                undoStack.snapshotBeforeChange(scene);
                object.visible = visible;
                dirty = true;
            }

            ImGui::TableNextColumn();
            bool isActive = (scene.activeObjectId == object.id);
            // NOT ImGuiSelectableFlags_SpanAllColumns: it extends this
            // Selectable's hit-test across every column in the row,
            // INCLUDING the Delete button and checkboxes drawn after it --
            // which was silently swallowing clicks meant for those
            // widgets (a known Dear ImGui table gotcha). A plain
            // Selectable confined to its own column trades "click
            // anywhere in the row to select" for "the other buttons in
            // this row actually work."
            if (ImGui::Selectable(object.name.c_str(), isActive)) {
                scene.activeObjectId = object.id; // not undoable -- active object is a UI cursor, not scene data
            }

            ImGui::TableNextColumn();
            float color[3] = {object.color.r, object.color.g, object.color.b};
            ImGuiColorEditFlags colorFlags = ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel;
            bool colorChanged = ImGui::ColorEdit3("##color", color, colorFlags);
            if (ImGui::IsItemActivated()) undoStack.beginContinuousEdit(scene);
            if (colorChanged) {
                object.color = glm::vec3(color[0], color[1], color[2]);
                dirty = true;
            }
            if (ImGui::IsItemDeactivatedAfterEdit()) undoStack.commitContinuousEdit();

            ImGui::TableNextColumn();
            ImGui::BeginDisabled(i == 0);
            if (ImGui::ArrowButton("##up", ImGuiDir_Up)) {
                undoStack.snapshotBeforeChange(scene);
                std::swap(scene.objects[i], scene.objects[i - 1]);
                dirty = true;
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(i + 1 >= scene.objects.size());
            if (ImGui::ArrowButton("##down", ImGuiDir_Down)) {
                undoStack.snapshotBeforeChange(scene);
                std::swap(scene.objects[i], scene.objects[i + 1]);
                dirty = true;
            }
            ImGui::EndDisabled();

            ImGui::TableNextColumn();
            if (i + 1 < scene.objects.size()) {
                int nextId = scene.objects[i + 1].id;
                bool linked = scene.objectLinks.count({object.id, nextId}) > 0;
                if (ImGui::Checkbox("##link", &linked)) {
                    undoStack.snapshotBeforeChange(scene);
                    scene.toggleLink(object.id, nextId);
                    dirty = true;
                }
            } else {
                ImGui::TextDisabled("--");
            }

            ImGui::TableNextColumn();
            if (ImGui::SmallButton("Delete")) {
                objectIdPendingDelete = object.id;
            }

            ImGui::PopID();
        }
        ImGui::EndTable();

        // Deleting inside the loop above would invalidate scene.objects'
        // iteration (indices shift), so the actual erase happens once,
        // after the table is fully drawn for this frame.
        if (objectIdPendingDelete != -1) {
            undoStack.snapshotBeforeChange(scene);
            auto it = std::find_if(scene.objects.begin(), scene.objects.end(),
                                    [objectIdPendingDelete](const SceneObject& o) { return o.id == objectIdPendingDelete; });
            if (it != scene.objects.end()) {
                scene.objects.erase(it);
                if (scene.activeObjectId == objectIdPendingDelete) {
                    scene.activeObjectId = scene.objects.empty() ? 0 : scene.objects.front().id;
                }
                dirty = true;
            }
        }
    }

    // Toggling "Link->next" above only records the pair in
    // scene.objectLinks -- a PROCEDURAL preview (drawn as a bright
    // magenta line in the viewport, see LinkPreviewRenderer), not real
    // path data. This button converts every currently-pending link into
    // a permanent Travel path on its from-object (editor/ObjectLinking.h),
    // after which it behaves exactly like any other path -- editable,
    // exportable, deletable.
    if (!scene.objectLinks.empty()) {
        ImGui::TextDisabled("%zu pending link(s) (magenta preview in viewport).", scene.objectLinks.size());
        if (ImGui::SmallButton("Bake links to travels")) {
            undoStack.snapshotBeforeChange(scene);
            // Snapshot the pairs first -- bakeLinkToTravel() erases from
            // scene.objectLinks as it goes, which would invalidate an
            // in-progress iteration over the live set.
            std::vector<std::pair<int, int>> pending(scene.objectLinks.begin(), scene.objectLinks.end());
            for (const auto& [fromId, toId] : pending) {
                bakeLinkToTravel(scene, fromId, toId);
            }
            dirty = true;
        }
    }
}

void EditorUI::drawMultiPartPanel(Scene& scene, UndoStack& undoStack, bool& dirty) {
    ImGui::Separator();
    if (boldFont_) ImGui::PushFont(boldFont_);
    bool open = ImGui::CollapsingHeader("Copy the object", ImGuiTreeNodeFlags_DefaultOpen);
    if (boldFont_) ImGui::PopFont();
    if (!open) return;

    ImGui::TextWrapped("Duplicates the active part (a plain translated copy -- NOT a true mirror; an "
                        "earlier version flipped the copy, which could route the entry travel across the "
                        "copy's own already-printed material), spreads the copies out, and links them "
                        "layer by layer into one program -- part A layer 1, part B layer 1, back to A "
                        "layer 2, and so on -- so each part cools while the others print. The travels "
                        "between parts are tagged for cutting apart afterward, leaving separate finished "
                        "parts.");

    SceneObject* active = scene.activeObject();
    ImGui::InputInt("Total copies (incl. original)", &multiPartCopies_);
    multiPartCopies_ = std::clamp(multiPartCopies_, 2, 8);
    ImGui::DragFloat("Space between copies (mm)", &multiPartSafeDistanceMm_, 5.0f, 0.0f, 5000.0f, "%.0f");
    ImGui::DragFloat("Detour margin (mm)", &multiPartTravelClearanceMm_, 1.0f, 0.0f, 1000.0f, "%.0f");
    ImGui::TextDisabled("Cross-part travels are FLAT -- they never change Z. With 3+ parts, a move");
    ImGui::TextDisabled("that would cross a middle part detours around it in Y by this much.");
    ImGui::DragFloat("Travel speed", &multiPartTravelSpeed_, 0.01f, 0.01f, 5.0f, "%.2f");

    // ONE button for one intent. This used to be two ("Mirror the
    // object", then "Build interleaved print"), which made the second
    // step look optional and the first look broken when used alone --
    // reported as "mirror doesn't work". Copying without the
    // layer-by-layer linking isn't a thing anyone wanted here.
    ImGui::Spacing();
    ImGui::BeginDisabled(active == nullptr);
    if (ImGui::Button("Copy and link layer by layer")) {
        undoStack.snapshotBeforeChange(scene);
        MirrorInterleaveOptions options;
        options.copies = multiPartCopies_;
        options.gapMm = multiPartSafeDistanceMm_;
        options.detourMarginMm = multiPartTravelClearanceMm_;
        options.travelSpeed = multiPartTravelSpeed_;

        if (auto merged = mirrorAndInterleave(scene, scene.activeObjectId, options)) {
            // Hide the sources rather than deleting them -- the merged
            // object is the thing to export, but destroying the
            // originals would make this hard to recover from.
            for (auto& object : scene.objects) object.visible = false;
            scene.addObject(std::move(*merged));
            scene.activeObjectId = scene.objects.back().id;
            lastMirrorResult_ = "Built: " + scene.objects.back().name;
        } else {
            lastMirrorResult_ = "Failed -- the active object has no detected print layers.";
        }
        dirty = true;
    }
    ImGui::EndDisabled();
    if (active == nullptr) ImGui::TextDisabled("Load a file and select an object first.");
    if (!lastMirrorResult_.empty()) ImGui::TextDisabled("%s", lastMirrorResult_.c_str());
}

void EditorUI::drawCellTemplatePanel(Scene& scene, SceneObject& object, const BedSettings& bed,
                                      UndoStack& undoStack, bool& dirty) {
    if (objectHasBoilerplate(object)) return; // nothing to warn about -- stay out of the way

    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.85f, 0.55f, 0.15f, 1.0f));
    ImGui::BeginChild("##missingBoilerplate", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
    ImGui::TextColored(ImVec4(1.0f, 0.70f, 0.30f, 1.0f), "Missing header/footer");
    ImGui::TextWrapped("This object has no &ACCESS, safety interrupts, or shutdown block of its own -- "
                        "a plain sliced import, or a merge whose sources all lacked one. Exporting it as-is "
                        "will very likely fail to load on the robot, or finish printing without ever "
                        "turning off the extruder/bed heat/cooling.");

    if (bed.cellTemplate.captured) {
        if (ImGui::Button("Fix using cell template")) {
            undoStack.snapshotBeforeChange(scene);
            applyCellTemplate(object, bed.cellTemplate);
            dirty = true;
        }
    } else {
        ImGui::TextDisabled("Capture a cell template first (Bed panel > Cell template) from a known-good file.");
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::Spacing();
}

// Real request: "a comment section that can be read by GcodeForge...
// and deleted if I want to." The actual write-into-the-.src logic lives
// in editor/SrcExporter.h/parser/SrcParser.cpp; this is just the text
// box. No undo snapshot on every keystroke (matches the existing
// layer-action text field's precedent -- undo-per-character would be
// noisy, not useful), and no `dirty` trigger either: a comment never
// affects rendering or export validation, only what gets written the
// next time the file is saved.
void EditorUI::drawCommentPanel(SceneObject& object) {
    if (boldFont_) ImGui::PushFont(boldFont_);
    bool open = ImGui::CollapsingHeader("Comment");
    if (boldFont_) ImGui::PopFont();
    if (!open) return;

    // Buffer only resyncs from object.comment when the ACTIVE OBJECT
    // changes -- otherwise every frame would stomp in-progress typing
    // with whatever object.comment already says (which IS what the
    // buffer itself just wrote, but re-copying every frame is still
    // wasted work and a trap if that ever stops being true).
    if (commentBufferObjectId_ != object.id) {
        std::snprintf(commentBuffer_, sizeof(commentBuffer_), "%s", object.comment.c_str());
        commentBufferObjectId_ = object.id;
    }

    ImGui::TextWrapped("Saved INSIDE the exported .src as a comment block -- travels with the file, "
                        "not just this project.");
    if (ImGui::InputTextMultiline("##objectComment", commentBuffer_, sizeof(commentBuffer_), ImVec2(-1, 70))) {
        object.comment = commentBuffer_;
    }

    ImGui::BeginDisabled(object.comment.empty());
    if (ImGui::Button("Delete comment")) {
        object.comment.clear();
        commentBuffer_[0] = '\0';
    }
    ImGui::EndDisabled();
}

void EditorUI::drawTransformPanel(Scene& scene, SceneObject& object, UndoStack& undoStack, bool& dirty) {
    if (boldFont_) ImGui::PushFont(boldFont_);
    bool transformOpen = ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen);
    if (boldFont_) ImGui::PopFont();
    if (!transformOpen) return;

    Transform& t = object.transform;
    ImGui::PushID("transform");

    auto dragField = [&](const char* label, double* value) {
        bool changed = ImGui::InputDouble(label, value, 1.0, 10.0, "%.2f");
        if (ImGui::IsItemActivated()) undoStack.beginContinuousEdit(scene);
        if (changed) dirty = true;
        if (ImGui::IsItemDeactivatedAfterEdit()) undoStack.commitContinuousEdit();
    };
    dragField("X (mm)", &t.x);
    dragField("Y (mm)", &t.y);
    dragField("Z (mm)", &t.z);
    dragField("Rotate Z (deg)", &t.rotZDegrees);

    bool flipX = t.flipX;
    if (ImGui::Checkbox("Flip X", &flipX)) {
        undoStack.snapshotBeforeChange(scene);
        t.flipX = flipX;
        dirty = true;
    }
    ImGui::SameLine();
    bool flipY = t.flipY;
    if (ImGui::Checkbox("Flip Y", &flipY)) {
        undoStack.snapshotBeforeChange(scene);
        t.flipY = flipY;
        dirty = true;
    }

    auto nudge = [&](const char* label, double* value, double delta) {
        if (ImGui::Button(label)) {
            undoStack.snapshotBeforeChange(scene);
            *value += delta;
            dirty = true;
        }
        ImGui::SameLine();
    };
    ImGui::Text("Nudge:");
    ImGui::SameLine();
    nudge("X-50", &t.x, -50.0);
    nudge("X+50", &t.x, 50.0);
    nudge("Y-50", &t.y, -50.0);
    nudge("Y+50", &t.y, 50.0);
    nudge("Z-50", &t.z, -50.0);
    if (ImGui::Button("Z+50")) {
        undoStack.snapshotBeforeChange(scene);
        t.z += 50.0;
        dirty = true;
    }

    if (ImGui::Button("Reset transform")) {
        undoStack.snapshotBeforeChange(scene);
        t = Transform{};
        dirty = true;
    }

    // Start point (the joint-space "first safe position") -- editable
    // here because transforming an object can carry it off the bed, and
    // the operator needs to be able to put it back. Coordinates are shown
    // in WORLD space (what "is it on the bed?" is actually asking) but
    // stored local, so the round-trip through the object's transform is
    // done on the way in and out.
    if (object.startPoint.present) {
        ImGui::Spacing();
        sectionLabel("Start point (first safe position)");
        const JointPose& j = object.startPoint.joints;
        ImGui::TextDisabled("Joint pose: A1 %.2f  A2 %.2f  A3 %.2f", j.a1, j.a2, j.a3);
        ImGui::TextDisabled("            A4 %.2f  A5 %.2f  A6 %.2f", j.a4, j.a5, j.a6);

        if (object.startPoint.position.has_value()) {
            glm::dvec3 world = applyTransform(t, *object.startPoint.position);
            float xyz[3] = {static_cast<float>(world.x), static_cast<float>(world.y), static_cast<float>(world.z)};
            bool changed = ImGui::DragFloat3("World X/Y/Z", xyz, 1.0f, 0.0f, 0.0f, "%.2f");
            if (ImGui::IsItemActivated()) undoStack.beginContinuousEdit(scene);
            if (changed) {
                object.startPoint.position = inverseApplyTransform(t, glm::dvec3(xyz[0], xyz[1], xyz[2]));
                object.startPoint.movedByOperator = true;
                dirty = true;
            }
            if (ImGui::IsItemDeactivatedAfterEdit()) undoStack.commitContinuousEdit();

            if (object.startPoint.movedByOperator) {
                ImGui::TextDisabled("Moved by hand (no longer the auto-derived anchor).");
                ImGui::SameLine();
                if (ImGui::SmallButton("Reset##startpoint")) {
                    undoStack.snapshotBeforeChange(scene);
                    // Back to the anchor the parser chose: the program's
                    // first Cartesian point.
                    if (!object.paths.empty()) {
                        object.startPoint.position = object.paths.front().to;
                        object.startPoint.movedByOperator = false;
                        dirty = true;
                    }
                }
            }
        } else {
            ImGui::TextDisabled("No Cartesian anchor -- this program has no Cartesian move to place it near.");
        }
        ImGui::TextDisabled("Display/planning only: export still writes the original joint move.");
    }

    ImGui::PopID();
}

void EditorUI::drawLayerTablePanel(Scene& scene, SceneObject& object, UndoStack& undoStack, bool& dirty, bool& selectionDirty) {
    if (boldFont_) ImGui::PushFont(boldFont_);
    bool layersOpen = ImGui::CollapsingHeader("Layers", ImGuiTreeNodeFlags_DefaultOpen);
    if (boldFont_) ImGui::PopFont();
    if (!layersOpen) return;

    if (object.layers.empty()) {
        ImGui::TextDisabled("No print layers detected.");
        return;
    }

    // A stale isolation set from a DIFFERENT object would apply to
    // whatever layer numbers happen to coincide in this one -- reset
    // rather than carry it across an active-object switch.
    if (isolatedLayersObjectId_ != object.id) {
        isolatedLayers_.clear();
        isolatedLayersObjectId_ = object.id;
    }

    ImGui::TextWrapped("Click a row to select that layer's print paths "
                        "(Shift = range-select from the last clicked layer, Ctrl = subtract).");
    ImGui::TextWrapped("Iso isolates a layer -- click more to see several at once, click again to remove one.");

    if (ImGui::BeginTable("layers", 9, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY,
                           ImVec2(0, 200))) {
        ImGui::TableSetupColumn("Visible", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableSetupColumn("Iso", ImGuiTableColumnFlags_WidthFixed, 40.0f);
        ImGui::TableSetupColumn("Layer");
        ImGui::TableSetupColumn("Z");
        ImGui::TableSetupColumn("Start");
        ImGui::TableSetupColumn("End");
        ImGui::TableSetupColumn("Paths");
        ImGui::TableSetupColumn("Speeds", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("Speed range", ImGuiTableColumnFlags_WidthFixed, 130.0f);
        ImGui::TableHeadersRow();

        for (const auto& layer : object.layers) {
            ImGui::TableNextRow();
            ImGui::PushID(layer.layer);

            ImGui::TableNextColumn();
            bool layerVisible = !isLayerHidden(object, layer.layer);
            if (ImGui::Checkbox("##layerVisible", &layerVisible)) {
                undoStack.snapshotBeforeChange(scene);
                setLayerHidden(object, layer.layer, !layerVisible);
                // Keep the Iso button's highlight (and "Show all layers"'
                // enabled state) truthful even when visibility was toggled
                // via this checkbox instead of Iso -- isolatedLayers_ means
                // "currently isolated/shown while isolating," so it must
                // track whichever control actually changed it.
                if (!isolatedLayers_.empty()) {
                    if (layerVisible) isolatedLayers_.insert(layer.layer);
                    else isolatedLayers_.erase(layer.layer);
                }
                dirty = true;
            }

            ImGui::TableNextColumn();
            bool isIsolated = isolatedLayers_.count(layer.layer) > 0;
            if (isIsolated) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.55f, 0.85f, 1.0f));
            bool isoClicked = ImGui::SmallButton("Iso");
            if (isIsolated) ImGui::PopStyleColor();
            if (isoClicked) {
                undoStack.snapshotBeforeChange(scene);
                if (isIsolated) {
                    // Un-isolating this one: if it was the LAST isolated
                    // layer, exit isolation mode entirely (show
                    // everything) instead of leaving nothing visible.
                    isolatedLayers_.erase(layer.layer);
                    if (isolatedLayers_.empty()) showAllPaths(object);
                    else setLayerHidden(object, layer.layer, true);
                } else {
                    bool enteringIsolation = isolatedLayers_.empty();
                    isolatedLayers_.insert(layer.layer);
                    if (enteringIsolation) {
                        // First layer isolated this round: hide every
                        // OTHER layer, show only this one.
                        for (const auto& other : object.layers) {
                            setLayerHidden(object, other.layer, other.layer != layer.layer);
                        }
                    } else {
                        // Already isolating others -- just add this one
                        // to what's visible, leave the rest as they are.
                        setLayerHidden(object, layer.layer, false);
                    }
                }
                dirty = true;
            }

            ImGui::TableNextColumn();
            char label[32];
            std::snprintf(label, sizeof(label), "%d", layer.layer);
            bool anySelected = false;
            for (int p = layer.startPath; p <= layer.endPath; ++p) {
                if (object.selectedPaths.count(p)) { anySelected = true; break; }
            }
            if (ImGui::Selectable(label, anySelected, ImGuiSelectableFlags_SpanAllColumns)) {
                undoStack.snapshotBeforeChange(scene);
                SelectionCompose compose = currentSelectionCompose();
                if (compose == SelectionCompose::Add && layerSelectionAnchor_ >= 0) {
                    // Shift-click: select every layer between the last
                    // clicked layer (the anchor) and this one, inclusive --
                    // standard range-select (click 3, shift-click 37, get
                    // 3 through 37), not just Add-this-one-layer.
                    int lo = std::min(layerSelectionAnchor_, layer.layer);
                    int hi = std::max(layerSelectionAnchor_, layer.layer);
                    std::vector<int> targets;
                    for (const auto& rangeLayer : object.layers) {
                        if (rangeLayer.layer < lo || rangeLayer.layer > hi) continue;
                        std::vector<int> layerPaths = pathNumbersForLayer(object, rangeLayer.layer);
                        targets.insert(targets.end(), layerPaths.begin(), layerPaths.end());
                    }
                    // A hidden path is out of reach for selection entirely --
                    // otherwise a range-select through a hidden layer would
                    // still grab (and let you transform/speed-edit) paths
                    // you can't even see. Reported from real use.
                    targets.erase(std::remove_if(targets.begin(), targets.end(),
                                   [&](int n) { return object.hiddenPaths.count(n) > 0; }), targets.end());
                    applySelectionCompose(object.selectedPaths, targets, SelectionCompose::Add);
                } else {
                    std::vector<int> targets = pathNumbersForLayer(object, layer.layer);
                    targets.erase(std::remove_if(targets.begin(), targets.end(),
                                   [&](int n) { return object.hiddenPaths.count(n) > 0; }), targets.end());
                    applySelectionCompose(object.selectedPaths, targets, compose);
                    layerSelectionAnchor_ = layer.layer; // plain/ctrl clicks move the anchor; shift-click ranges from the last one
                }
                selectionDirty = true;
            }

            ImGui::TableNextColumn();
            ImGui::Text("%.3f", layer.z);
            ImGui::TableNextColumn();
            ImGui::Text("%d", layer.startPath);
            ImGui::TableNextColumn();
            ImGui::Text("%d", layer.endPath);
            ImGui::TableNextColumn();
            ImGui::Text("%d", layer.endPath - layer.startPath + 1);

            // A layer is rarely one single speed -- a real sliced layer
            // mixes perimeter/infill speeds, and an operator edit can
            // split it further. Showing the DISTINCT COUNT plus the
            // min-max range answers both "is this layer uniform?" and
            // "what is it actually running at?" without needing a row
            // per path.
            std::vector<double> layerSpeeds;
            for (const auto& p : object.paths) {
                if (p.type != PathType::Print || p.layer != layer.layer) continue;
                double s = p.effectiveSpeed();
                bool seen = false;
                for (double existing : layerSpeeds) {
                    if (std::abs(existing - s) < 1e-9) { seen = true; break; }
                }
                if (!seen) layerSpeeds.push_back(s);
            }
            std::sort(layerSpeeds.begin(), layerSpeeds.end());

            ImGui::TableNextColumn();
            if (layerSpeeds.empty()) {
                ImGui::TextDisabled("--");
            } else if (layerSpeeds.size() == 1) {
                ImGui::Text("1");
            } else {
                // Mixed speeds are worth spotting at a glance -- it's
                // usually intentional (perimeter vs infill) but it's also
                // how a half-applied speed edit shows up.
                ImGui::TextColored(ImVec4(1.0f, 0.80f, 0.35f, 1.0f), "%zu", layerSpeeds.size());
            }

            ImGui::TableNextColumn();
            if (layerSpeeds.empty()) {
                ImGui::TextDisabled("--");
            } else if (layerSpeeds.size() == 1) {
                ImGui::Text("%.4f", layerSpeeds.front());
            } else {
                ImGui::Text("%.4f - %.4f", layerSpeeds.front(), layerSpeeds.back());
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::TextUnformatted("Distinct speeds in this layer:");
                    for (double s : layerSpeeds) ImGui::Text("  %.4f", s);
                    ImGui::EndTooltip();
                }
            }

            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    // Bulk hide/show -- ONE undo snapshot and ONE geometry rebuild for
    // every layer at once, instead of clicking each layer's Visible
    // checkbox individually. Reported from real use: hiding many layers
    // one checkbox at a time was very slow -- each click does a full
    // Scene snapshot (UndoStack.h copies the whole scene) and a full
    // geometry rebuild, so doing that N times in a row costs N times as
    // much as doing it once.
    ImGui::BeginDisabled(object.layers.empty());
    if (ImGui::SmallButton("Hide all layers")) {
        undoStack.snapshotBeforeChange(scene);
        std::vector<int> all;
        for (const auto& l : object.layers) {
            std::vector<int> layerPaths = pathNumbersForLayer(object, l.layer);
            all.insert(all.end(), layerPaths.begin(), layerPaths.end());
        }
        hidePaths(object, all);
        dirty = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(object.hiddenPaths.empty() && isolatedLayers_.empty());
    if (ImGui::SmallButton("Show all layers")) {
        undoStack.snapshotBeforeChange(scene);
        isolatedLayers_.clear();
        showAllPaths(object);
        dirty = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (!isolatedLayers_.empty()) {
        ImGui::TextDisabled("Isolating %zu layer(s).", isolatedLayers_.size());
    } else if (!object.hiddenPaths.empty()) {
        ImGui::TextDisabled("%zu path(s) hidden.", object.hiddenPaths.size());
    } else {
        ImGui::TextDisabled("Nothing hidden.");
    }

    ImGui::Spacing();
    ImGui::Separator();
    sectionLabel("Z Offset");
    ImGui::TextWrapped("Correct a layer's actual measured Z (e.g. it printed at 0.205mm, not the intended 0.200mm).");

    int maxLayer = object.layers.empty() ? 1 : object.layers.back().layer;
    ImGui::InputInt("Starting layer##zoffset", &zOffsetStartLayer_);
    zOffsetStartLayer_ = std::clamp(zOffsetStartLayer_, 1, maxLayer);
    ImGui::InputDouble("Delta Z (mm)##zoffset", &zOffsetDeltaMm_, 0.001, 0.01, "%.4f");

    const char* kZOffsetModeLabels[] = {
        "Just this layer",
        "This and every layer above it",
        "This and the next N layers above",
        "This and the next N layers above, tapering to 0",
    };
    ImGui::Combo("Applies to##zoffset", &zOffsetModeIndex_, kZOffsetModeLabels, 4);

    bool needsCount = (zOffsetModeIndex_ == 2 || zOffsetModeIndex_ == 3);
    ImGui::BeginDisabled(!needsCount);
    ImGui::InputInt("N (layers above)##zoffset", &zOffsetLayerCount_);
    zOffsetLayerCount_ = std::max(zOffsetLayerCount_, 1);
    ImGui::EndDisabled();

    ImGui::BeginDisabled(std::abs(zOffsetDeltaMm_) < 1e-9);
    if (ImGui::Button("Apply Z offset")) {
        undoStack.snapshotBeforeChange(scene);
        ZOffsetOptions options;
        options.startLayer = zOffsetStartLayer_;
        options.deltaZMm = zOffsetDeltaMm_;
        options.layerCount = zOffsetLayerCount_;
        options.mode = static_cast<ZOffsetMode>(zOffsetModeIndex_);
        applyLayerZOffset(object, options);
        dirty = true;
    }
    ImGui::EndDisabled();

    ImGui::Spacing();
    sectionLabel("Selection & splitting");
    ImGui::Text("Selected paths: %zu", object.selectedPaths.size());
    ImGui::SameLine();
    if (ImGui::SmallButton("Select all visible")) {
        undoStack.snapshotBeforeChange(scene);
        applySelectionCompose(object.selectedPaths, allPathNumbers(object), currentSelectionCompose());
        selectionDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear")) {
        undoStack.snapshotBeforeChange(scene);
        object.selectedPaths.clear();
        selectionDirty = true;
    }
    // Travels carry no layer, so the layer table can never select them --
    // these are the only way to grab them as a group, which matters
    // because a long travel is a common thing to want to slow down or
    // split. Both respect Shift/Ctrl compose like every other selector.
    if (ImGui::SmallButton("Select travels")) {
        undoStack.snapshotBeforeChange(scene);
        applySelectionCompose(object.selectedPaths, travelPathNumbers(object), currentSelectionCompose());
        selectionDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Select prints")) {
        undoStack.snapshotBeforeChange(scene);
        applySelectionCompose(object.selectedPaths, printPathNumbers(object), currentSelectionCompose());
        selectionDirty = true;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(object.selectedPaths.empty());
    if (ImGui::SmallButton("Split selected")) {
        undoStack.snapshotBeforeChange(scene);
        splitSelectedPaths(object);
        dirty = true; // structural change (paths vector grew) -- needs a full rebuild, not just a selection refresh
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled("Split inserts a new vertex at each selected path's midpoint -- handy for");
    ImGui::TextDisabled("giving half of a long travel/print move its own speed. Turn on");
    ImGui::TextDisabled("View > Display > Vertices to see the new point appear.");

    ImGui::Spacing();
    ImGui::BeginDisabled(object.selectedPaths.empty());
    if (ImGui::SmallButton("Hide selected")) {
        undoStack.snapshotBeforeChange(scene);
        hideSelectedPaths(object);
        dirty = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(object.hiddenPaths.empty());
    if (ImGui::SmallButton("Show all")) {
        undoStack.snapshotBeforeChange(scene);
        showAllPaths(object);
        dirty = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::Text("(%zu hidden)", object.hiddenPaths.size());
    // Hiding is a VIEWPORT-ONLY aid, same as the object list's Visible
    // checkbox above it -- a hidden path still exports and still prints.
    // It exists to declutter a busy scene while working, not to exclude
    // geometry from the job; use Layer actions or actually delete paths
    // for that.
    ImGui::TextDisabled("Hiding only affects this view -- hidden paths still export and print.");

    ImGui::Spacing();
    ImGui::SetNextItemWidth(100.0f);
    ImGui::DragFloat("##rotateAngle", &rotateSelectedAngleDeg_, 0.5f, -360.0f, 360.0f, "%.1f deg");
    ImGui::SameLine();
    ImGui::BeginDisabled(object.selectedPaths.empty());
    if (ImGui::SmallButton("Rotate selected")) {
        undoStack.snapshotBeforeChange(scene);
        rotateSelectedPaths(object, rotateSelectedAngleDeg_);
        dirty = true;
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled("Spins the selected paths around THEIR OWN centroid (not the object's pivot).");
    ImGui::TextDisabled("Can leave a gap where a rotated path used to touch an unselected neighbor --");
    ImGui::TextDisabled("same trade-off as Start/End gizmo dragging.");

    ImGui::Spacing();
    sectionLabel("Layer actions");
    ImGui::TextWrapped("Insert a command before a layer's first motion line on export "
                        "(HALT, part cooling, or custom KRL text).");

    // Output indices are EDITABLE, not hardcoded: these default to the
    // mapping an Eidos-generated program uses for this cell (its
    // end-of-program block labels $OUT[5] "AIR COMMAND", $OUT[6] bed
    // heat, $OUT[7] extruder motor), but I/O assignment is per-cell and
    // firing the wrong output on a different machine could do something
    // genuinely unwanted. Surfaced up front so the operator confirms it
    // rather than discovers it.
    ImGui::TextDisabled("Cell I/O mapping -- confirm this matches YOUR cell:");
    ImGui::SetNextItemWidth(140.0f);
    ImGui::InputInt("Cooling/air $OUT", &coolingOutputIndex_);
    coolingOutputIndex_ = std::clamp(coolingOutputIndex_, 1, 4096);

    char coolingOn[128], coolingOff[128];
    std::snprintf(coolingOn, sizeof(coolingOn), "$OUT[%d]=TRUE", coolingOutputIndex_);
    std::snprintf(coolingOff, sizeof(coolingOff), "$OUT[%d]=FALSE", coolingOutputIndex_);

    const char* kPresetLabels[] = {"Halt", "Part cooling ON", "Part cooling OFF", "Custom"};
    const char* kPresetText[] = {"HALT", coolingOn, coolingOff, ""};
    if (ImGui::Combo("Preset", &layerActionPresetIndex_, kPresetLabels, 4)) {
        std::snprintf(layerActionTextBuffer_, sizeof(layerActionTextBuffer_), "%s", kPresetText[layerActionPresetIndex_]);
    }
    ImGui::InputInt("Target layer", &layerActionTargetLayer_);
    layerActionTargetLayer_ = std::clamp(layerActionTargetLayer_, 1, object.layers.empty() ? 1 : object.layers.back().layer);
    ImGui::InputText("KRL text", layerActionTextBuffer_, sizeof(layerActionTextBuffer_));

    // The bug this guards against, found the hard way on a real print:
    // the old presets inserted "; TODO: set the correct output..." --
    // which is a KRL COMMENT. It exported fine, the robot ran fine, and
    // part cooling simply never switched on, with nothing anywhere
    // saying why. Text that is empty or entirely a comment cannot DO
    // anything on the robot, so refuse to add it rather than let it look
    // like it worked.
    std::string trimmedAction = layerActionTextBuffer_;
    trimmedAction.erase(0, trimmedAction.find_first_not_of(" \t"));
    bool actionIsNoOp = trimmedAction.empty() || trimmedAction[0] == ';';

    if (actionIsNoOp) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.30f, 1.0f),
                            "Empty or comment-only -- this would do NOTHING on the robot.");
    }
    ImGui::BeginDisabled(actionIsNoOp);
    if (ImGui::Button("Add layer action")) {
        undoStack.snapshotBeforeChange(scene);
        LayerAction action;
        action.layer = layerActionTargetLayer_;
        action.label = kPresetLabels[layerActionPresetIndex_];
        action.krlText = layerActionTextBuffer_;
        object.layerActions.push_back(action);
        dirty = true;
    }
    ImGui::EndDisabled();

    for (size_t i = 0; i < object.layerActions.size(); ++i) {
        const LayerAction& action = object.layerActions[i];
        ImGui::PushID(static_cast<int>(i) + 10000);
        ImGui::Text("Layer %d: %s (%s)", action.layer, action.label.c_str(), action.krlText.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove")) {
            undoStack.snapshotBeforeChange(scene);
            object.layerActions.erase(object.layerActions.begin() + static_cast<long>(i));
            dirty = true;
            ImGui::PopID();
            break; // vector shrank -- stop iterating this frame
        }
        ImGui::PopID();
    }
}

void EditorUI::drawSelectionGroupPanel(Scene& scene, SceneObject& object, UndoStack& undoStack, bool& dirty, bool& selectionDirty) {
    if (boldFont_) ImGui::PushFont(boldFont_);
    bool groupsOpen = ImGui::CollapsingHeader("Selection groups");
    if (boldFont_) ImGui::PopFont();
    if (!groupsOpen) return;

    ImGui::InputText("Name", groupNameBuffer_, sizeof(groupNameBuffer_));
    ImGui::ColorEdit3("Color", groupColor_);
    ImGui::BeginDisabled(object.selectedPaths.empty());
    if (ImGui::Button("Create group from current selection")) {
        undoStack.snapshotBeforeChange(scene);
        createSelectionGroupFromSelection(object, groupNameBuffer_,
                                           glm::vec3(groupColor_[0], groupColor_[1], groupColor_[2]));
        dirty = true;
    }
    ImGui::EndDisabled();

    for (size_t i = 0; i < object.selectionGroups.size(); ++i) {
        SelectionGroup& group = object.selectionGroups[i];
        ImGui::PushID(group.id.c_str());
        ImGui::Text("%s (%zu paths)", group.name.c_str(), group.pathNumbers.size());
        ImGui::SameLine();
        if (ImGui::SmallButton("Select")) {
            undoStack.snapshotBeforeChange(scene);
            applySelectionCompose(object.selectedPaths, group.pathNumbers, currentSelectionCompose());
            selectionDirty = true;
        }
        ImGui::SameLine();
        bool groupVisible = !isGroupHidden(object, group);
        if (ImGui::Checkbox("##groupVisible", &groupVisible)) {
            undoStack.snapshotBeforeChange(scene);
            setGroupHidden(object, group, !groupVisible);
            dirty = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Delete")) {
            undoStack.snapshotBeforeChange(scene);
            object.selectionGroups.erase(object.selectionGroups.begin() + static_cast<long>(i));
            ImGui::PopID();
            dirty = true;
            break; // vector shrank, indices are stale -- stop iterating this frame
        }
        ImGui::PopID();
    }
}

void EditorUI::drawSpeedPanel(Scene& scene, SceneObject& object, UndoStack& undoStack, bool& dirty) {
    if (boldFont_) ImGui::PushFont(boldFont_);
    bool speedOpen = ImGui::CollapsingHeader("Speed editing", ImGuiTreeNodeFlags_DefaultOpen);
    if (boldFont_) ImGui::PopFont();
    if (!speedOpen) return;

    if (object.selectedPaths.empty()) {
        ImGui::TextDisabled("Select paths (viewport click/drag, layer table, or group) to edit speed.");
        return;
    }

    std::vector<int> targets(object.selectedPaths.begin(), object.selectedPaths.end());

    // Spell out WHAT is about to be edited. Speed edits apply to travels
    // and print paths alike (only PTP is skipped -- $VEL.CP doesn't
    // control point-to-point motion), so "12 paths selected" alone
    // doesn't tell you whether you're about to change a print speed, a
    // travel speed, or both.
    int printCount = 0, travelCount = 0, ptpCount = 0;
    for (int number : targets) {
        const Path* p = static_cast<const SceneObject&>(object).findPath(number);
        if (!p) continue;
        if (p->motion == "PTP") ++ptpCount;
        else if (p->type == PathType::Travel) ++travelCount;
        else ++printCount;
    }
    ImGui::Text("Selected: %d print, %d travel", printCount, travelCount);
    if (ptpCount > 0) {
        ImGui::TextDisabled("(%d PTP path(s) will be skipped -- $VEL.CP doesn't control PTP motion)", ptpCount);
    }

    ImGui::InputDouble("Exact speed (m/s)", &speedExact_, 0.001, 0.01, "%.4f");
    if (ImGui::Button("Apply exact")) {
        undoStack.snapshotBeforeChange(scene);
        SpeedApplyResult result = applySpeedToPaths(object, targets, SpeedApplyMode::Exact, speedExact_);
        std::printf("Applied exact speed to %d path(s), skipped %d PTP path(s)\n",
                    result.appliedCount, result.skippedPtpCount);
        dirty = true;
    }

    ImGui::InputDouble("Percent", &speedPercent_, 1.0, 5.0, "%.1f");
    ImGui::SameLine();
    if (ImGui::Button("Reduce")) {
        undoStack.snapshotBeforeChange(scene);
        applySpeedToPaths(object, targets, SpeedApplyMode::Reduce, speedPercent_);
        dirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Increase")) {
        undoStack.snapshotBeforeChange(scene);
        applySpeedToPaths(object, targets, SpeedApplyMode::Increase, speedPercent_);
        dirty = true;
    }
}

void EditorUI::drawBedConformPanel(Scene& scene, SceneObject& object, const BedHeightmap& heightmap,
                                    const BedSettings& bed, UndoStack& undoStack, bool& dirty) {
    if (boldFont_) ImGui::PushFont(boldFont_);
    bool open = ImGui::CollapsingHeader("Bed Conform");
    if (boldFont_) ImGui::PopFont();
    if (!open) return;

    if (heightmap.cols < 2 || heightmap.rows < 2) {
        ImGui::TextDisabled("No bed heightmap data yet -- enter measurements in the Bed panel first.");
        return;
    }

    ImGui::TextWrapped("Shifts each print path's Z (and optionally speed) based on the measured bed "
                        "heightmap at its position -- higher bed runs faster, lower bed runs slower.");

    ImGui::InputInt("Affected layers", &bedConformAffectedLayers_);
    bedConformAffectedLayers_ = std::max(bedConformAffectedLayers_, 1);
    ImGui::TextDisabled("Layer 1 gets full effect, tapering to none by layer (affected+1).");

    ImGui::Checkbox("Adjust Z", &bedConformAdjustZ_);
    ImGui::SameLine();
    ImGui::Checkbox("Adjust speed", &bedConformAdjustSpeed_);

    ImGui::BeginDisabled(!bedConformAdjustSpeed_);
    ImGui::DragFloat("Speed gain per mm", &bedConformSpeedGainPerMm_, 0.01f, 0.0f, 1.0f, "%.2f");
    ImGui::EndDisabled();

    ImGui::BeginDisabled(!bedConformAdjustZ_ && !bedConformAdjustSpeed_);
    if (ImGui::Button(object.bedConform.has_value() ? "Re-apply bed conform" : "Apply bed conform")) {
        undoStack.snapshotBeforeChange(scene);
        // Re-applying while a conform is already active must start from
        // the object's TRUE pre-conform state, not from wherever the
        // active (possibly rescaled) conform currently left it -- revert
        // first, exactly like the user clicking Delete, then apply fresh.
        if (object.bedConform.has_value()) removeBedConform(object);
        BedConformOptions options;
        options.affectedLayers = bedConformAffectedLayers_;
        options.adjustZ = bedConformAdjustZ_;
        options.adjustSpeed = bedConformAdjustSpeed_;
        options.speedGainPerMm = bedConformSpeedGainPerMm_;
        applyBedConformRecorded(object, heightmap, bed, options);
        dirty = true;
    }
    ImGui::EndDisabled();

    // The active conform stays a re-visitable, adjustable "layer" until
    // explicitly baked or deleted -- requested from real use: "I can
    // delete it or multiply it or decrease it. I can bake it to make it
    // part of the object."
    if (object.bedConform.has_value()) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.45f, 0.75f, 0.95f, 1.0f), "Bed conform layer active (%zu path(s))",
                            object.bedConform->perPath.size());

        float scale = static_cast<float>(object.bedConform->scale);
        if (ImGui::DragFloat("Effect strength", &scale, 0.01f, 0.0f, 3.0f, "%.2fx")) {
            undoStack.snapshotBeforeChange(scene);
            setBedConformScale(object, static_cast<double>(scale));
            dirty = true;
        }
        ImGui::TextDisabled("1.00x = as applied. 0 removes the effect without deleting the layer; "
                             "above 1 multiplies it, below 1 weakens it.");

        if (ImGui::Button("Delete")) {
            undoStack.snapshotBeforeChange(scene);
            removeBedConform(object);
            dirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Bake into object")) {
            undoStack.snapshotBeforeChange(scene);
            bakeBedConform(object);
            dirty = true;
        }
        ImGui::TextDisabled("Bake keeps the current effect permanently and stops tracking it as adjustable.");
    }
}

void EditorUI::drawColorModePanel(ColorMode& colorMode, bool& dirty) {
    sectionLabel("Color mode");
    const ColorMode modes[] = {ColorMode::Object, ColorMode::Type, ColorMode::Layer,
                                ColorMode::Group, ColorMode::Speed, ColorMode::Sequence};
    for (ColorMode mode : modes) {
        bool selected = (colorMode == mode);
        if (ImGui::RadioButton(colorModeLabel(mode), selected)) {
            if (!selected) {
                colorMode = mode;
                dirty = true;
            }
        }
    }
}

void EditorUI::drawStatsPanel(const Scene& scene, RenderMode mode, size_t renderedPrimitiveCount) {
    size_t totalPaths = 0, totalLayers = 0;
    for (const auto& object : scene.objects) {
        totalPaths += object.paths.size();
        totalLayers += object.layers.size();
    }
    ImGui::Text("%zu object(s), %zu path(s), %zu layer(s)", scene.objects.size(), totalPaths, totalLayers);
    if (mode == RenderMode::Lines) {
        ImGui::Text("%zu line segment(s) on GPU", renderedPrimitiveCount);
    } else {
        ImGui::Text("%zu triangle(s) on GPU (bead geometry)", renderedPrimitiveCount);
    }
}
