// Step 2: a working orbit camera looking at a reference grid.
// Controls: left-drag orbit, right-drag pan, scroll to zoom,
// keys 1/2/3/4 jump to Top/Front/Right/Iso views.

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>
#include <cstdio>

#include "render/Camera.h"
#include "render/GridRenderer.h"

namespace {

Camera* g_camera = nullptr;
double g_lastCursorX = 0.0;
double g_lastCursorY = 0.0;

void onGlfwError(int code, const char* description) {
    std::fprintf(stderr, "GLFW error %d: %s\n", code, description);
}

void onFramebufferResize(GLFWwindow*, int width, int height) {
    glViewport(0, 0, width, height);
    if (g_camera && height > 0) {
        g_camera->setViewportSize(static_cast<float>(width), static_cast<float>(height));
    }
}

void onScroll(GLFWwindow*, double, double yOffset) {
    if (g_camera) {
        g_camera->zoom(static_cast<float>(yOffset));
    }
}

void onKey(GLFWwindow*, int key, int, int action, int) {
    if (action != GLFW_PRESS || !g_camera) return;
    switch (key) {
        case GLFW_KEY_1: g_camera->setPreset(Camera::Preset::Top); break;
        case GLFW_KEY_2: g_camera->setPreset(Camera::Preset::Front); break;
        case GLFW_KEY_3: g_camera->setPreset(Camera::Preset::Right); break;
        case GLFW_KEY_4: g_camera->setPreset(Camera::Preset::Iso); break;
        default: break;
    }
}

// Sanity-check the camera math without needing eyes on the screen: print
// where the camera ends up for each preset. "Top" should end up almost
// directly above the origin looking straight down (forward.z near -1).
void debugPrintPresets(Camera& camera) {
    struct Named { const char* name; Camera::Preset preset; };
    const Named presets[] = {
        {"Top", Camera::Preset::Top},
        {"Front", Camera::Preset::Front},
        {"Right", Camera::Preset::Right},
        {"Iso", Camera::Preset::Iso},
    };
    std::printf("-- camera preset sanity check --\n");
    for (const auto& p : presets) {
        camera.setPreset(p.preset);
        std::printf("%-6s eye=%s forward=%s\n", p.name,
                    glm::to_string(camera.eyePosition()).c_str(),
                    glm::to_string(camera.forwardVector()).c_str());
    }
    camera.setPreset(Camera::Preset::Iso);
    std::printf("---------------------------------\n");
}

} // namespace

int main() {
    glfwSetErrorCallback(onGlfwError);

    if (!glfwInit()) {
        std::fprintf(stderr, "Failed to initialize GLFW\n");
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(1280, 800, "Gcode Editor (C++)", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        std::fprintf(stderr, "Failed to initialize GLEW\n");
        return 1;
    }

    std::printf("OpenGL renderer: %s\n", glGetString(GL_RENDERER));
    std::printf("OpenGL version:  %s\n", glGetString(GL_VERSION));

    Camera camera;
    g_camera = &camera;
    debugPrintPresets(camera);

    GridRenderer grid;

    int fbWidth = 0, fbHeight = 0;
    glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
    camera.setViewportSize(static_cast<float>(fbWidth), static_cast<float>(fbHeight));
    glViewport(0, 0, fbWidth, fbHeight);

    glfwSetFramebufferSizeCallback(window, onFramebufferResize);
    glfwSetScrollCallback(window, onScroll);
    glfwSetKeyCallback(window, onKey);

    glfwGetCursorPos(window, &g_lastCursorX, &g_lastCursorY);
    glEnable(GL_DEPTH_TEST);
    glLineWidth(1.0f);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        double cursorX, cursorY;
        glfwGetCursorPos(window, &cursorX, &cursorY);
        double dx = cursorX - g_lastCursorX;
        double dy = cursorY - g_lastCursorY;
        g_lastCursorX = cursorX;
        g_lastCursorY = cursorY;

        if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
            camera.orbit(static_cast<float>(dx), static_cast<float>(dy));
        } else if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
            camera.pan(static_cast<float>(dx), static_cast<float>(dy));
        }

        glClearColor(0.09f, 0.10f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        if (height > 0) {
            glm::mat4 viewProj = camera.projectionMatrix(static_cast<float>(width), static_cast<float>(height)) * camera.viewMatrix();
            grid.draw(viewProj);
        }

        glfwSwapBuffers(window);
    }

    g_camera = nullptr;
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
