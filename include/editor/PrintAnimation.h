#pragma once

#include "model/SceneObject.h"

#include <glm/glm.hpp>
#include <vector>

// A print-time simulation: subdivides every path into sub-segments short
// enough to reveal gradually (a single long straight LIN move, drawn
// whole, would just pop into existence -- not a realistic simulation of
// printing), and gives playback and timeline-scrubbing ONE shared
// time-to-position function so they can never drift apart from each
// other.
//
// The web Gcode Editor has an animation module already (a moving marker
// dot over the ALREADY-fully-drawn geometry, driven by distance, no time
// readout) -- this is deliberately more than a port: real per-path print
// TIME (length / speed), a timeline that scrubs by time and shows
// percentage-complete + elapsed/remaining, and actual progressive
// geometry reveal, not just a marker.

struct AnimationSegment {
    int sourcePathNumber = 0;  // which real Path this sub-segment came from
    glm::dvec3 from{0.0};      // world space (already transformed)
    glm::dvec3 to{0.0};
    PathType type = PathType::Print;

    double startDistanceMm = 0.0; // cumulative distance from sequence start, at `from`
    double lengthMm = 0.0;
    double startTimeSeconds = 0.0; // cumulative time from sequence start, at `from`
    double speedMmPerS = 0.0;      // > 0 always (fallback applied if the path had no real speed)
};

struct AnimationSequence {
    std::vector<AnimationSegment> segments;
    double totalDistanceMm = 0.0;
    double totalTimeSeconds = 0.0;
};

// The state of the simulation at one moment -- everything the renderer
// and the HUD need, computed fresh from a single time value so seeking
// (scrub) and stepping (play) are the exact same operation.
struct PlaybackState {
    double timeSeconds = 0.0;
    double coveredDistanceMm = 0.0;
    double progress = 0.0; // 0..1, time-based (timeSeconds / totalTimeSeconds)
    int segmentIndex = -1; // -1 if the sequence is empty
    double segmentT = 0.0; // 0..1 within the current segment
    glm::dvec3 headPosition{0.0};
    glm::dvec3 headDirection{1.0, 0.0, 0.0}; // unit vector, current segment's direction (for orienting the head mesh)
    bool finished = false;

    int sourcePathNumber = 0;
    PathType currentType = PathType::Print;
};

// Builds the subdivided, world-space animation sequence for `object`'s
// paths, in file order, restricted by `includePrint`/`includeTravel`
// (mirrors RenderSettings' own print/travel visibility filters, so what
// you've hidden in the viewport is also skipped in the simulation).
// Every path longer than `maxSegmentLengthMm` is split into equal-length
// sub-segments (a path shorter than the limit stays whole -- one
// segment). `fallbackSpeedMps` is used for any path with no real speed
// recorded (matches the source's `speed.value_or(0)` -- printing "at
// zero speed" would make the whole simulation stall on that one path).
AnimationSequence buildAnimationSequence(const SceneObject& object, double maxSegmentLengthMm,
                                          double fallbackSpeedMps, bool includePrint, bool includeTravel);

// The core shared primitive: given an absolute time (clamped to
// [0, totalTimeSeconds]), finds the segment that time falls in (binary
// search over each segment's startTimeSeconds -- segments are always in
// increasing time order, since they're built by walking the sequence in
// order and accumulating distance/time) and interpolates the head
// position within it. Called every frame during playback (with
// accumulated wall-clock time * speed multiplier) AND on every scrub-bar
// drag (with whatever time the bar position maps to) -- same function,
// so play and scrub can never show a different position for the same
// time value.
PlaybackState stateAtTime(const AnimationSequence& sequence, double timeSeconds);
