#include "editor/UndoStack.h"

void UndoStack::push(const Scene& scene) {
    undoStack_.push_back(scene);
    if (undoStack_.size() > kMaxHistory) {
        undoStack_.erase(undoStack_.begin());
    }
    redoStack_.clear();
}

void UndoStack::snapshotBeforeChange(const Scene& scene) {
    push(scene);
}

void UndoStack::beginContinuousEdit(const Scene& scene) {
    if (continuousEditActive_) return; // already capturing this edit
    continuousEditPreState_ = scene;
    continuousEditActive_ = true;
}

void UndoStack::commitContinuousEdit() {
    if (!continuousEditActive_) return;
    push(continuousEditPreState_);
    continuousEditActive_ = false;
}

void UndoStack::cancelContinuousEdit() {
    continuousEditActive_ = false;
}

void UndoStack::undo(Scene& scene) {
    if (undoStack_.empty()) return;
    redoStack_.push_back(scene);
    scene = undoStack_.back();
    undoStack_.pop_back();
}

void UndoStack::redo(Scene& scene) {
    if (redoStack_.empty()) return;
    undoStack_.push_back(scene);
    scene = redoStack_.back();
    redoStack_.pop_back();
}
