#pragma once

#include "model/Scene.h"
#include "render/LineShader.h"
#include "render/PathColorizer.h"

#include <GL/glew.h>
#include <glm/glm.hpp>

// Draws every visible path in a Scene as GL_LINES, colored per the active
// ColorMode. This is the real toolpath renderer -- GridRenderer was
// throwaway scaffolding for milestone 2, kept only as a ground reference.
//
// Deliberately simple for now: rebuild() re-uploads the whole vertex buffer
// from scratch. Fine for the file sizes we're testing with; milestone 11
// (viewport LOD) is where this stops being good enough for 100k+ segment
// production files.
class SceneRenderer {
public:
    SceneRenderer();
    ~SceneRenderer();

    SceneRenderer(const SceneRenderer&) = delete;
    SceneRenderer& operator=(const SceneRenderer&) = delete;

    // Rebuilds the GPU vertex buffer from the current scene state. Call
    // whenever paths, visibility, transforms, or the color mode change --
    // NOT every frame.
    void rebuild(const Scene& scene, ColorMode colorMode);

    void draw(const glm::mat4& viewProj) const;

    size_t lineCount() const { return static_cast<size_t>(vertexCount_) / 2; }

private:
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint shaderProgram_ = 0;
    GLint mvpUniformLocation_ = -1;
    GLsizei vertexCount_ = 0;
    GLsizei vboCapacityVertices_ = 0;

    SpeedColorTable speedColors_;
};
