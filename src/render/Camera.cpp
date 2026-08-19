#include "render/Camera.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace {
// At yaw=0, pitch=0 the camera looks along -Y with +Z as "up on screen".
// Everything else is this vector rotated by the orbit orientation.
const glm::vec3 kBaseForward(0.0f, -1.0f, 0.0f);
const glm::vec3 kBaseUp(0.0f, 0.0f, 1.0f);

constexpr float kBaseHalfExtentMm = 400.0f; // ortho world half-width visible at zoomFactor_ == 1
constexpr float kBaseDistanceMm = 2000.0f;  // perspective orbit radius at zoomFactor_ == 1
constexpr float kOrbitSensitivity = 0.007f;
constexpr float kPanSensitivity = 1.0f;     // extra multiplier on top of the per-pixel world scale
constexpr float kMinZoom = 0.02f;
constexpr float kMaxZoom = 50.0f;
constexpr float kPitchLimitDeg = 89.5f; // stop just short of the pole; math wouldn't break, but the UX gets disorienting there
constexpr float kFovYDegrees = 45.0f;
}

Camera::Camera()
    : target_(0.0f, 0.0f, 0.0f),
      yaw_(glm::radians(45.0f)),
      pitch_(glm::radians(35.264f)), // classic isometric tilt (atan(1/sqrt(2)))
      zoomFactor_(1.0f),
      viewportWidth_(1280.0f),
      viewportHeight_(800.0f) {}

glm::quat Camera::orientation() const {
    // Pitch is applied first, in the camera's own local space, THEN yaw
    // rotates that result around the world's vertical axis. Quaternion
    // multiplication order q_yaw * q_pitch means "do q_pitch, then q_yaw" —
    // that ordering is what makes yaw always spin around world Z regardless
    // of current pitch, which is the behavior an orbit camera wants.
    glm::quat qYaw = glm::angleAxis(yaw_, glm::vec3(0.0f, 0.0f, 1.0f));
    glm::quat qPitch = glm::angleAxis(pitch_, glm::vec3(1.0f, 0.0f, 0.0f));
    return qYaw * qPitch;
}

float Camera::currentDistance() const {
    return kBaseDistanceMm / zoomFactor_;
}

void Camera::orbit(float dxPixels, float dyPixels) {
    yaw_ -= dxPixels * kOrbitSensitivity;
    pitch_ += dyPixels * kOrbitSensitivity;
    const float limit = glm::radians(kPitchLimitDeg);
    pitch_ = std::clamp(pitch_, -limit, limit);
}

void Camera::pan(float dxPixels, float dyPixels) {
    glm::quat q = orientation();
    glm::vec3 right = q * glm::vec3(1.0f, 0.0f, 0.0f);
    glm::vec3 up = q * kBaseUp;

    // Convert a pixel delta to a world-space delta so panning tracks the
    // cursor 1:1 regardless of zoom level: at 2x zoom, the same pixel drag
    // should move the target half as far in world units. Uses the ortho
    // half-extent as the "world units per pixel" reference even in
    // perspective mode -- close enough for a pan gesture, and keeps pan
    // speed consistent when switching projections.
    float halfHeight = kBaseHalfExtentMm / zoomFactor_;
    float worldPerPixel = (halfHeight * 2.0f) / viewportHeight_ * kPanSensitivity;

    // Sign convention: dragging right/up should feel like grabbing the
    // scene and dragging it with the cursor (the content follows the
    // hand), so the target moves the SAME direction as the cursor delta.
    target_ += right * (dxPixels * worldPerPixel);
    target_ -= up * (dyPixels * worldPerPixel);
}

void Camera::zoom(float delta) {
    zoomFactor_ *= std::pow(1.1f, delta);
    zoomFactor_ = std::clamp(zoomFactor_, kMinZoom, kMaxZoom);
}

void Camera::setPreset(Preset preset) {
    switch (preset) {
        case Preset::Top:
            // Looking straight down the world Z axis at the XY (bed) plane.
            yaw_ = 0.0f;
            pitch_ = glm::radians(kPitchLimitDeg);
            break;
        case Preset::Front:
            yaw_ = 0.0f;
            pitch_ = 0.0f;
            break;
        case Preset::Right:
            yaw_ = glm::radians(90.0f);
            pitch_ = 0.0f;
            break;
        case Preset::Iso:
            yaw_ = glm::radians(45.0f);
            pitch_ = glm::radians(35.264f);
            break;
    }
}

glm::vec3 Camera::forwardVector() const {
    return orientation() * kBaseForward;
}

glm::vec3 Camera::eyePosition() const {
    return target_ - forwardVector() * currentDistance();
}

glm::mat4 Camera::viewMatrix() const {
    glm::quat q = orientation();
    glm::vec3 forward = q * kBaseForward;
    glm::vec3 up = q * kBaseUp; // derived from the SAME rotation as forward, so it's always valid, even at the poles
    glm::vec3 eye = target_ - forward * currentDistance();
    return glm::lookAt(eye, target_, up);
}

glm::mat4 Camera::projectionMatrix(float viewportWidth, float viewportHeight) const {
    float aspect = viewportWidth / viewportHeight;

    if (projection_ == Projection::Perspective) {
        return glm::perspective(glm::radians(kFovYDegrees), aspect, 1.0f, 200000.0f);
    }

    // Orthographic: for inspecting toolpaths, parallel lines staying
    // parallel (no perspective foreshortening) matters more than a
    // "cinematic" look -- this is why CAD/CAM tools default to it.
    float halfHeight = kBaseHalfExtentMm / zoomFactor_;
    float halfWidth = halfHeight * aspect;
    return glm::ortho(-halfWidth, halfWidth, -halfHeight, halfHeight, -200000.0f, 200000.0f);
}

void Camera::setViewportSize(float width, float height) {
    viewportWidth_ = width;
    viewportHeight_ = height;
}
