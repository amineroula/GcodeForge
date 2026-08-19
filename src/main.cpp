// Step 3: load a real SRC file into the scene model and render it as
// colored 3D lines. This is the first milestone where the app actually
// shows a toolpath instead of placeholder scaffolding.
//
// Controls: left-drag orbit, right-drag pan, scroll to zoom,
// keys 1/2/3/4 jump to Top/Front/Right/Iso views, key C cycles color mode,
// key G toggles the reference grid.

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>
#include <cstdio>
#include <string>

#include "io/FileIO.h"
#include "model/Scene.h"
#include "parser/SrcParser.h"
#include "render/Camera.h"
#include "render/GridRenderer.h"
#include "render/PathColorizer.h"
#include "render/SceneRenderer.h"

namespace {

Camera* g_camera = nullptr;
double g_lastCursorX = 0.0;
double g_lastCursorY = 0.0;

ColorMode g_colorMode = ColorMode::Layer;
bool g_colorModeDirty = false;
bool g_showGrid = true;

const char* colorModeName(ColorMode mode) {
    switch (mode) {
        case ColorMode::Object: return "Object";
        case ColorMode::Type: return "Type";
        case ColorMode::Layer: return "Layer";
        case ColorMode::Group: return "Group";
        case ColorMode::Speed: return "Speed";
    }
    return "?";
}

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
    if (action != GLFW_PRESS) return;
    switch (key) {
        case GLFW_KEY_1: if (g_camera) g_camera->setPreset(Camera::Preset::Top); break;
        case GLFW_KEY_2: if (g_camera) g_camera->setPreset(Camera::Preset::Front); break;
        case GLFW_KEY_3: if (g_camera) g_camera->setPreset(Camera::Preset::Right); break;
        case GLFW_KEY_4: if (g_camera) g_camera->setPreset(Camera::Preset::Iso); break;
        case GLFW_KEY_C: {
            int next = (static_cast<int>(g_colorMode) + 1) % 5;
            g_colorMode = static_cast<ColorMode>(next);
            g_colorModeDirty = true;
            std::printf("Color mode: %s\n", colorModeName(g_colorMode));
            break;
        }
        case GLFW_KEY_G: g_showGrid = !g_showGrid; break;
        default: break;
    }
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

    GridRenderer grid;
    SceneRenderer sceneRenderer;
    Scene scene;

    {
        std::string samplePath = std::string(ASSETS_DIR) + "/samples/sample_chair.src";
        std::vector<std::string> lines = readLinesFromFile(samplePath);
        if (lines.empty()) {
            std::fprintf(stderr, "Could not read sample file: %s\n", samplePath.c_str());
        } else {
            SceneObject object = parseSrc("Chair_01", lines);
            std::printf("Loaded %s: %zu paths, %zu layers\n",
                        object.name.c_str(), object.paths.size(), object.layers.size());
            scene.addObject(std::move(object));
            sceneRenderer.rebuild(scene, g_colorMode);
            std::printf("Uploaded %zu line segments to the GPU. Color mode: %s\n",
                        sceneRenderer.lineCount(), colorModeName(g_colorMode));
        }
    }

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

        if (g_colorModeDirty) {
            sceneRenderer.rebuild(scene, g_colorMode);
            g_colorModeDirty = false;
        }

        glClearColor(0.09f, 0.10f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        if (height > 0) {
            glm::mat4 viewProj = camera.projectionMatrix(static_cast<float>(width), static_cast<float>(height)) * camera.viewMatrix();
            if (g_showGrid) grid.draw(viewProj);
            sceneRenderer.draw(viewProj);
        }

        glfwSwapBuffers(window);
    }

    g_camera = nullptr;
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
