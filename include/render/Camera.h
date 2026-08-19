#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

// An orbit camera: it always looks at a "target" point in the world and
// moves around it on a sphere. This is the standard camera for CAD/CAM/DCC
// tools (as opposed to a first-person "fly" camera).
//
// Orientation is stored as yaw (spin around world Z) + pitch (tilt up/down),
// then converted to a quaternion each time we need vectors from it. Building
// the forward/up vectors from the SAME quaternion (instead of yaw/pitch plus
// a separately fixed world "up" vector fed into lookAt) is what keeps this
// stable even when looking straight down or straight up — the classic bug
// in naive orbit cameras is that fixed-up lookAt() breaks (produces NaN)
// exactly at the "top" view, which is usually the first view someone wants.
//
// Input is Maya-style: Alt+LMB orbits, Alt+MMB pans, Alt+RMB (or plain
// scroll) dollies/zooms. That's handled in main.cpp's input polling; this
// class just exposes orbit()/pan()/zoom() as raw operations.
class Camera {
public:
    Camera();

    // Mouse-drag orbit. Pass raw pixel deltas; sign/scale is handled inside.
    void orbit(float dxPixels, float dyPixels);

    // Mouse-drag pan (moves the look-at target, not just the camera).
    void pan(float dxPixels, float dyPixels);

    // Zoom/dolly. Positive = zoom in. Drives both the orbit distance (used
    // by perspective) and the view half-extent (used by orthographic), via
    // the same zoomFactor_, so switching projection mid-session doesn't
    // reset how "zoomed in" the view feels.
    void zoom(float delta);

    enum class Preset { Top, Front, Right, Iso };
    void setPreset(Preset preset);

    enum class Projection { Perspective, Orthographic };
    void setProjection(Projection projection) { projection_ = projection; }
    Projection projection() const { return projection_; }
    void toggleProjection() {
        projection_ = (projection_ == Projection::Perspective) ? Projection::Orthographic : Projection::Perspective;
    }

    glm::mat4 viewMatrix() const;
    glm::mat4 projectionMatrix(float viewportWidth, float viewportHeight) const;

    void setViewportSize(float width, float height);

    // For debugging/teaching: where is the camera actually looking?
    glm::vec3 eyePosition() const;
    glm::vec3 forwardVector() const;

private:
    glm::quat orientation() const;
    float currentDistance() const; // orbit radius, shrinks/grows with zoomFactor_

    glm::vec3 target_;   // world-space point the camera orbits around / looks at
    float yaw_;          // radians, spin around world +Z
    float pitch_;        // radians, tilt around the camera's local +X
    float zoomFactor_;   // >1 = zoomed in, <1 = zoomed out
    float viewportWidth_;
    float viewportHeight_;
    Projection projection_ = Projection::Perspective; // Maya's default camera is perspective
};
