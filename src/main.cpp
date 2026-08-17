// Step 1: open a window and get an OpenGL context talking to us.
// Nothing is drawn yet — this just proves the toolchain (CMake, GLFW, GLEW) works end to end.

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <cstdio>

static void onGlfwError(int code, const char* description) {
    std::fprintf(stderr, "GLFW error %d: %s\n", code, description);
}

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
    glfwSwapInterval(1); // vsync

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        std::fprintf(stderr, "Failed to initialize GLEW\n");
        return 1;
    }

    std::printf("OpenGL renderer: %s\n", glGetString(GL_RENDERER));
    std::printf("OpenGL version:  %s\n", glGetString(GL_VERSION));

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        glClearColor(0.09f, 0.10f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glfwSwapBuffers(window);
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
