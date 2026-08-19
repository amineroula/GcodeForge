cmake_minimum_required(VERSION 3.20)
project(GcodeCpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# GLFW — window + input
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# GLM — vector/matrix math
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
)
FetchContent_MakeAvailable(glm)

# GLEW — OpenGL function loader (CMake-packaged fork, no codegen step needed)
FetchContent_Declare(
    glew
    GIT_REPOSITORY https://github.com/Perlmint/glew-cmake.git
    GIT_TAG glew-cmake-2.2.0
)
set(glew-cmake_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ONLY_LIBS ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glew)

# Dear ImGui — editor UI
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.1
)
FetchContent_MakeAvailable(imgui)
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC glfw libglew_static)
target_compile_definitions(imgui PUBLIC IMGUI_IMPL_OPENGL_LOADER_GLEW GLEW_STATIC)

# gcode_core: model + parser + io. Pure logic, no OpenGL/GLFW/ImGui
# dependency at all -- this is what makes it possible to unit-test the
# parser without spinning up a graphics context (see docs/PLAN.md's
# architecture section).
file(GLOB_RECURSE GCODE_CORE_SOURCES CONFIGURE_DEPENDS
    src/model/*.cpp src/parser/*.cpp src/io/*.cpp src/editor/*.cpp)
add_library(gcode_core STATIC ${GCODE_CORE_SOURCES})
target_include_directories(gcode_core PUBLIC include)
target_link_libraries(gcode_core PUBLIC glm::glm)

file(GLOB_RECURSE GCODE_APP_SOURCES CONFIGURE_DEPENDS
    src/render/*.cpp src/ui/*.cpp src/main.cpp)

add_executable(gcode_editor ${GCODE_APP_SOURCES})
target_include_directories(gcode_editor PRIVATE include)
target_link_libraries(gcode_editor PRIVATE gcode_core glfw libglew_static imgui)
target_compile_definitions(gcode_editor PRIVATE GLEW_STATIC ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets")
if(WIN32)
    target_link_libraries(gcode_editor PRIVATE comdlg32)
endif()

enable_testing()
add_executable(parser_smoke_test tests/parser_smoke_test.cpp)
target_link_libraries(parser_smoke_test PRIVATE gcode_core)
add_test(NAME parser_smoke_test COMMAND parser_smoke_test)

if(MSVC)
    target_compile_options(gcode_editor PRIVATE /W4)
    target_compile_options(gcode_core PRIVATE /W4)
endif()
