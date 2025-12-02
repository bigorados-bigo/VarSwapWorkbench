set(VARSWAP_WORKBENCH_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
set(VARSWAP_SRC_ROOT "${VARSWAP_WORKBENCH_ROOT}/src/varswap")

set(VARSWAP_EXTERNAL_HA6_ROOT "" CACHE PATH "Optional path to an external Hantei-chan source tree")

if(VARSWAP_EXTERNAL_HA6_ROOT)
    set(HA6_ROOT "${VARSWAP_EXTERNAL_HA6_ROOT}")
    set(HA6_SRC_ROOT "${HA6_ROOT}/src")
else()
    set(HA6_ROOT "${VARSWAP_WORKBENCH_ROOT}")
    set(HA6_SRC_ROOT "${HA6_ROOT}/vendor/hantei/src")
endif()

set(HA6_CORE_SOURCES
    "${HA6_SRC_ROOT}/framedata.cpp"
    "${HA6_SRC_ROOT}/framedata_load.cpp"
    "${HA6_SRC_ROOT}/framedata_save.cpp"
    "${HA6_SRC_ROOT}/misc.cpp"
    "${HA6_SRC_ROOT}/filedialog.cpp"
    "${HA6_SRC_ROOT}/ui/font_loader.cpp"
)

if(VARSWAP_EXTERNAL_HA6_ROOT)
    list(APPEND HA6_CORE_SOURCES "${HA6_SRC_ROOT}/framestate.cpp")
endif()

add_library(ha6_core STATIC ${HA6_CORE_SOURCES})

target_include_directories(ha6_core
    PUBLIC
        "${HA6_SRC_ROOT}"
        "${HA6_ROOT}"
        "${VARSWAP_WORKBENCH_ROOT}/src"
        "${HA6_ROOT}/config"
        "${HA6_ROOT}/third_party"
        "${HA6_ROOT}/imgui"
        "${HA6_ROOT}/imgui/backends"
        "${HA6_ROOT}/res"
)

target_compile_definitions(ha6_core
    PUBLIC
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
)

target_link_libraries(ha6_core
    PUBLIC
        glm
        tinyalloc
)

set(VARSWAP_MODULE_SOURCES
    "${VARSWAP_SRC_ROOT}/occurrence.cpp"
    "${VARSWAP_SRC_ROOT}/varswap_pane.cpp"
)

add_library(varswap_module STATIC ${VARSWAP_MODULE_SOURCES})

add_library(varswap::module ALIAS varswap_module)
add_library(ha6::core ALIAS ha6_core)

target_link_libraries(varswap_module
    PUBLIC
        ha6_core
        imgui
)

target_include_directories(varswap_module
    PUBLIC
    "${HA6_SRC_ROOT}"
    "${HA6_ROOT}/config"
    "${HA6_ROOT}/third_party"
    "${HA6_ROOT}/imgui"
    "${HA6_ROOT}/imgui/backends"
        "${VARSWAP_WORKBENCH_ROOT}/src"
)

target_compile_definitions(varswap_module PUBLIC UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
