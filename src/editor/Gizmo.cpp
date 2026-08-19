#include "editor/Gizmo.h"

#include <glm/gtc/matrix_inverse.hpp>

namespace {
float pointToSegmentDistance(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b) {
    glm::vec2 ab = b - a;
    float lenSq = glm::dot(ab, ab);
    float t = (lenSq > 1e-9f) ? glm::dot(p - a, ab) / lenSq : 0.0f;
    t = glm::clamp(t, 0.0f, 1.0f);
    glm::vec2 closest = a + ab * t;
    return glm::length(p - closest);
}
} // namespace

Ray unprojectRay(const glm::mat4& viewProj, glm::vec2 screenPoint, float viewportWidth, float viewportHeight) {
    glm::mat4 invViewProj = glm::inverse(viewProj);

    float ndcX = (screenPoint.x / viewportWidth) * 2.0f - 1.0f;
    float ndcY = 1.0f - (screenPoint.y / viewportHeight) * 2.0f; // screen is y-down, NDC is y-up

    glm::vec4 nearClip = invViewProj * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
    glm::vec4 farClip = invViewProj * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
    glm::vec3 nearWorld = glm::vec3(nearClip) / nearClip.w;
    glm::vec3 farWorld = glm::vec3(farClip) / farClip.w;

    Ray ray;
    ray.origin = nearWorld;
    ray.direction = glm::normalize(farWorld - nearWorld);
    return ray;
}

std::optional<float> closestPointOnAxisToRay(const glm::vec3& axisOrigin, const glm::vec3& axisDir, const Ray& ray) {
    // Standard closest-point-between-two-lines derivation. Line 1 is the
    // axis (P1 + t1*D1), line 2 is the ray (P2 + t2*D2). Solving
    // d/dt1 |L1-L2|^2 = 0 and d/dt2 |L1-L2|^2 = 0 simultaneously gives:
    glm::vec3 w0 = axisOrigin - ray.origin;
    float a = glm::dot(axisDir, axisDir);   // == 1, axisDir is unit length
    float b = glm::dot(axisDir, ray.direction);
    float c = glm::dot(ray.direction, ray.direction); // == 1, ray.direction is unit length
    float d = glm::dot(axisDir, w0);
    float e = glm::dot(ray.direction, w0);

    float denom = a * c - b * b;
    if (std::abs(denom) < 1e-6f) return std::nullopt; // ray parallel to axis -- no unique closest point

    float t1 = (b * e - c * d) / denom;
    return t1;
}

glm::vec3 gizmoAxisDirection(GizmoAxis axis) {
    switch (axis) {
        case GizmoAxis::X: return glm::vec3(1.0f, 0.0f, 0.0f);
        case GizmoAxis::Y: return glm::vec3(0.0f, 1.0f, 0.0f);
        case GizmoAxis::Z: return glm::vec3(0.0f, 0.0f, 1.0f);
    }
    return glm::vec3(1.0f, 0.0f, 0.0f);
}

std::optional<GizmoAxis> pickGizmoAxis(const std::vector<GizmoAxisScreenSegment>& segments,
                                        glm::vec2 screenPoint, float pickRadiusPixels) {
    std::optional<GizmoAxis> best;
    float bestDistance = pickRadiusPixels;
    for (const auto& segment : segments) {
        float distance = pointToSegmentDistance(screenPoint, segment.screenFrom, segment.screenTo);
        if (distance <= bestDistance) {
            bestDistance = distance;
            best = segment.axis;
        }
    }
    return best;
}
