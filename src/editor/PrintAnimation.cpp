#include "editor/PrintAnimation.h"
#include "model/Transform.h"

#include <algorithm>
#include <cmath>

AnimationSequence buildAnimationSequence(const SceneObject& object, double maxSegmentLengthMm,
                                          double fallbackSpeedMps, bool includePrint, bool includeTravel) {
    AnimationSequence seq;
    double cumDist = 0.0, cumTime = 0.0;
    const double maxLen = std::max(maxSegmentLengthMm, 0.01);
    const double fallback = std::max(fallbackSpeedMps, 1e-6);

    for (const auto& path : object.paths) {
        if (path.type == PathType::Print && !includePrint) continue;
        if (path.type == PathType::Travel && !includeTravel) continue;

        glm::dvec3 worldFrom = applyTransform(object.transform, path.from);
        glm::dvec3 worldTo = applyTransform(object.transform, path.to);
        double totalLen = glm::length(worldTo - worldFrom);
        if (totalLen <= 1e-9) continue; // zero-length -- nothing to reveal, no time to spend

        double speedMps = path.effectiveSpeed();
        double speedMmS = (speedMps > 1e-9 ? speedMps : fallback) * 1000.0;

        int subCount = std::max(1, static_cast<int>(std::ceil(totalLen / maxLen)));
        double subLen = totalLen / subCount;
        double subTime = subLen / speedMmS;

        for (int i = 0; i < subCount; ++i) {
            double t0 = static_cast<double>(i) / subCount;
            double t1 = static_cast<double>(i + 1) / subCount;

            AnimationSegment seg;
            seg.sourcePathNumber = path.number;
            seg.from = glm::mix(worldFrom, worldTo, t0);
            seg.to = glm::mix(worldFrom, worldTo, t1);
            seg.type = path.type;
            seg.startDistanceMm = cumDist;
            seg.lengthMm = subLen;
            seg.startTimeSeconds = cumTime;
            seg.speedMmPerS = speedMmS;
            seq.segments.push_back(seg);

            cumDist += subLen;
            cumTime += subTime;
        }
    }

    seq.totalDistanceMm = cumDist;
    seq.totalTimeSeconds = cumTime;
    return seq;
}

PlaybackState stateAtTime(const AnimationSequence& sequence, double timeSeconds) {
    PlaybackState state;
    if (sequence.segments.empty()) {
        state.finished = true;
        return state;
    }

    double clamped = std::clamp(timeSeconds, 0.0, sequence.totalTimeSeconds);
    state.timeSeconds = clamped;
    state.progress = (sequence.totalTimeSeconds > 1e-12) ? clamped / sequence.totalTimeSeconds : 1.0;
    state.finished = clamped >= sequence.totalTimeSeconds - 1e-9;

    // Last segment whose startTimeSeconds <= clamped -- segments are
    // always in non-decreasing time order (built by walking the sequence
    // once, accumulating), so a binary search is valid and keeps
    // scrubbing responsive on a real file's tens of thousands of
    // sub-segments.
    size_t lo = 0, hi = sequence.segments.size() - 1;
    while (lo < hi) {
        size_t mid = lo + (hi - lo + 1) / 2;
        if (sequence.segments[mid].startTimeSeconds <= clamped) lo = mid; else hi = mid - 1;
    }

    const AnimationSegment& seg = sequence.segments[lo];
    double segDuration = (seg.lengthMm > 1e-12 && seg.speedMmPerS > 1e-12) ? seg.lengthMm / seg.speedMmPerS : 0.0;
    double t = (segDuration > 1e-12) ? std::clamp((clamped - seg.startTimeSeconds) / segDuration, 0.0, 1.0) : 1.0;

    state.segmentIndex = static_cast<int>(lo);
    state.segmentT = t;
    state.coveredDistanceMm = seg.startDistanceMm + t * seg.lengthMm;
    state.headPosition = glm::mix(seg.from, seg.to, t);

    glm::dvec3 dir = seg.to - seg.from;
    double dirLen = glm::length(dir);
    state.headDirection = (dirLen > 1e-9) ? dir / dirLen : glm::dvec3(1.0, 0.0, 0.0);

    state.sourcePathNumber = seg.sourcePathNumber;
    state.currentType = seg.type;
    return state;
}
