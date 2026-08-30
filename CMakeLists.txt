cmake_minimum_required(VERSION 3.24)

# Enable CMake's MSVC runtime selection before project()/language initialization.
if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" VRFK_VERSION LIMIT_COUNT 1)
string(STRIP "${VRFK_VERSION}" VRFK_VERSION)
if(NOT VRFK_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "VERSION must contain MAJOR.MINOR.PATCH, got: ${VRFK_VERSION}")
endif()

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/TEST_BUILD" VRFK_TEST_BUILD LIMIT_COUNT 1)
string(STRIP "${VRFK_TEST_BUILD}" VRFK_TEST_BUILD)
if(NOT VRFK_TEST_BUILD MATCHES "^[0-9]+$")
    message(FATAL_ERROR "TEST_BUILD must contain a numeric build identifier, got: ${VRFK_TEST_BUILD}")
endif()

project(VRFullKeyboard VERSION ${VRFK_VERSION} LANGUAGES CXX)

# Official public update source. Keep these fixed so release builds can only
# query the verified VR Full Keyboard repository.
set(VRFK_GITHUB_OWNER "tw2323123")
set(VRFK_GITHUB_REPO "VRFullKeyboard")

# The native Control Center has no OpenVR/ImGui dependency. Bootstrap builds set
# this OFF so the first launch needs only MSVC + Windows SDK + CMake, with no Git/network.
option(VRFK_BUILD_CORE "Build VR keyboard core and fetch OpenVR/ImGui" ON)

# Static MSVC runtime: Release uses /MT and Debug uses /MTd.
# This removes the normal dependency on the VC++ Redistributable for friends.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(NOT WIN32)
    message(FATAL_ERROR "VRFullKeyboard currently targets Windows + SteamVR only.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Single source of truth for the application version.
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/VRFullKeyboardVersion.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/VRFullKeyboardVersion.h
    @ONLY
)


# Native Windows control center. This is the single day-to-day front end for
# launching previews, building the core, packaging releases, and opening outputs.
add_executable(VRFullKeyboardControl WIN32
    src/control_center.cpp
)

target_include_directories(VRFullKeyboardControl PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)

target_compile_definitions(VRFullKeyboardControl PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
)

if(MSVC)
    target_compile_options(VRFullKeyboardControl PRIVATE /source-charset:utf-8 /execution-charset:utf-8)
endif()

target_link_libraries(VRFullKeyboardControl PRIVATE
    user32
    shell32
    ole32
    comctl32
    bcrypt
    dwmapi
    winhttp
)

# Small native updater. It runs from the downloaded staging directory so it can
# safely replace the currently installed control center and keyboard binaries.
add_executable(VRFullKeyboardUpdater WIN32
    src/updater.cpp
)

target_compile_definitions(VRFullKeyboardUpdater PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
)

if(MSVC)
    target_compile_options(VRFullKeyboardUpdater PRIVATE /source-charset:utf-8 /execution-charset:utf-8)
endif()

target_link_libraries(VRFullKeyboardUpdater PRIVATE
    user32
    shell32
)

if(VRFK_BUILD_CORE)
    include(FetchContent)

    # FetchContent_Populate with full population details remains supported on
    # modern CMake and avoids the deprecated single-argument CMP0169 path.
    FetchContent_Populate(openvr
        GIT_REPOSITORY https://github.com/ValveSoftware/openvr.git
        GIT_TAG v2.15.6
        GIT_SHALLOW TRUE
    )

    FetchContent_Populate(imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG v1.92.9b
        GIT_SHALLOW TRUE
    )

    add_executable(VRFullKeyboard WIN32
        src/main.cpp
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    )

    target_include_directories(VRFullKeyboard PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}/generated
        ${openvr_SOURCE_DIR}/headers
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
    )

    target_compile_definitions(VRFullKeyboard PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        UNICODE
        _UNICODE
    )

    # The source contains Traditional Chinese / Bopomofo legends.
    # Force MSVC to read source files as UTF-8 instead of the system ACP (e.g. CP950).
    if(MSVC)
        target_compile_options(VRFullKeyboard PRIVATE /source-charset:utf-8 /execution-charset:utf-8)
    endif()

    add_library(openvr_api SHARED IMPORTED)
    set_target_properties(openvr_api PROPERTIES
        IMPORTED_IMPLIB "${openvr_SOURCE_DIR}/lib/win64/openvr_api.lib"
        IMPORTED_LOCATION "${openvr_SOURCE_DIR}/bin/win64/openvr_api.dll"
    )

    target_link_libraries(VRFullKeyboard PRIVATE
        openvr_api
        d3d11
        dxgi
        user32
        imm32
        winhttp
        shell32
    )

    # Put openvr_api.dll next to the executable.
    add_custom_command(TARGET VRFullKeyboard POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:openvr_api>
            $<TARGET_FILE_DIR:VRFullKeyboard>
    )

endif()

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT VRFullKeyboardControl)
