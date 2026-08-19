#pragma once

#include "model/Scene.h"

#include <vector>

// Whole-scene snapshot undo/redo, matching the original's pushUndo()
// design (see docs/PLAN.md): Scene/SceneObject/Path are plain copyable
// data with no owned resources, so "undo" is just "restore a saved copy."
// Simple and correct; a diff-based history would be more memory-efficient
// on very large scenes but isn't needed yet -- edits are infrequent,
// human-paced actions, not a per-frame cost.
//
// Two ways to record a change:
//  - snapshotBeforeChange(): call once, immediately before a single
//    discrete action (a button click) takes effect.
//  - beginContinuousEdit()/commitContinuousEdit(): call begin once when a
//    drag/text-edit starts (e.g. ImGui::IsItemActivated()) and commit once
//    when it ends (ImGui::IsItemDeactivatedAfterEdit()) -- this avoids
//    pushing a new undo entry on every frame a slider is held.
class UndoStack {
public:
    void snapshotBeforeChange(const Scene& scene);

    void beginContinuousEdit(const Scene& scene);
    void commitContinuousEdit();
    void cancelContinuousEdit();

    bool canUndo() const { return !undoStack_.empty(); }
    bool canRedo() const { return !redoStack_.empty(); }

    // No-ops if there's nothing to undo/redo.
    void undo(Scene& scene);
    void redo(Scene& scene);

private:
    void push(const Scene& scene);

    std::vector<Scene> undoStack_;
    std::vector<Scene> redoStack_;
    bool continuousEditActive_ = false;
    Scene continuousEditPreState_;

    static constexpr size_t kMaxHistory = 100;
};
