cmake_minimum_required(VERSION 3.16)
project(varswap_workbench LANGUAGES C CXX VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")

set(VARSWAP_WORKBENCH_ROOT "${CMAKE_CURRENT_LIST_DIR}")

add_library(glad STATIC
    "${VARSWAP_WORKBENCH_ROOT}/glad/src/glad.c"
)

target_include_directories(glad PUBLIC
    "${VARSWAP_WORKBENCH_ROOT}/glad/include"
)

file(GLOB imgui_sources
    "${VARSWAP_WORKBENCH_ROOT}/imgui/*.cpp"
)

add_library(imgui STATIC
    "${VARSWAP_WORKBENCH_ROOT}/imgui/backends/imgui_impl_opengl3.cpp"
    "${VARSWAP_WORKBENCH_ROOT}/imgui/backends/imgui_impl_win32.cpp"
    "${VARSWAP_WORKBENCH_ROOT}/imgui/misc/cpp/imgui_stdlib.cpp"
    ${imgui_sources}
)

target_include_directories(imgui PUBLIC
    "${VARSWAP_WORKBENCH_ROOT}/imgui"
    "${VARSWAP_WORKBENCH_ROOT}/imgui/backends"
    "${VARSWAP_WORKBENCH_ROOT}/imgui/misc/cpp"
    "${VARSWAP_WORKBENCH_ROOT}/config"
)

target_compile_definitions(imgui PRIVATE IMGUI_IMPL_OPENGL_LOADER_GLAD IMGUI_IMPL_OPENGL_ES2 IMGUI_USER_CONFIG=<config.h>)
target_link_libraries(imgui PUBLIC glad imm32 dwmapi)

add_subdirectory("${VARSWAP_WORKBENCH_ROOT}/glm" "${CMAKE_BINARY_DIR}/glm")
add_subdirectory("${VARSWAP_WORKBENCH_ROOT}/tinyalloc" "${CMAKE_BINARY_DIR}/tinyalloc")
add_subdirectory(varswap_core)

find_package(OpenGL REQUIRED)

add_executable(varswap_workbench
    "${VARSWAP_WORKBENCH_ROOT}/tools/varswap/workbench_main.cpp"
    "${VARSWAP_WORKBENCH_ROOT}/tools/varswap/workbench_settings_stub.cpp"
    "${VARSWAP_WORKBENCH_ROOT}/res/res.rc"
)

target_include_directories(varswap_workbench PRIVATE
    "${VARSWAP_WORKBENCH_ROOT}/vendor/hantei/src"
    "${VARSWAP_WORKBENCH_ROOT}/config"
    "${VARSWAP_WORKBENCH_ROOT}/res"
    "${VARSWAP_WORKBENCH_ROOT}/src"
    "${VARSWAP_WORKBENCH_ROOT}/tools/varswap"
)

target_compile_definitions(varswap_workbench PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)

target_link_libraries(varswap_workbench PRIVATE imgui glad OpenGL::GL glm tinyalloc varswap_module)
set_target_properties(varswap_workbench PROPERTIES WIN32_EXECUTABLE TRUE)
