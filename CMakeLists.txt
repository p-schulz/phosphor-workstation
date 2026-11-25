cmake_minimum_required(VERSION 3.24)

if(APPLE)
    # GLFW's Cocoa backend is plain Objective-C; enable OBJC so it is compiled as such.
    project(phosphor VERSION 0.1.0 LANGUAGES C CXX OBJC)
else()
    project(phosphor VERSION 0.1.0 LANGUAGES C CXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(PHOS_WERROR "Treat warnings in own code as errors" OFF)
option(PHOS_BUILD_TESTS "Build phosphor_tests" ON)
option(PHOS_BUILD_APP "Build the windowed app (needs GLFW + OpenGL)" ON)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)
include(Warnings)
include(Dependencies)

set(PHOS_ASSET_DIR ${CMAKE_SOURCE_DIR}/assets)

# ---------------------------------------------------------------------------
# phosphor_core — core, dsp, synth, seq, model, engine, io. No GL, no device I/O.
# ---------------------------------------------------------------------------
add_library(phosphor_core STATIC
    src/core/text.cpp
    src/core/version.cpp
)
target_include_directories(phosphor_core PUBLIC src)
target_compile_definitions(phosphor_core PUBLIC
    PHOS_VERSION_STRING="${PROJECT_VERSION}")
target_link_libraries(phosphor_core PUBLIC nlohmann_json::nlohmann_json)
phos_set_warnings(phosphor_core)

# ---------------------------------------------------------------------------
# phosphor_ui — gfx + ui. No GL.
# ---------------------------------------------------------------------------
add_library(phosphor_ui STATIC
    src/gfx/canvas.cpp
    src/gfx/font.cpp
    src/gfx/image_io.cpp
    src/gfx/palette.cpp
    src/gfx/viewport.cpp
    src/ui/chrome.cpp
    src/ui/command_line.cpp
    src/ui/commands.cpp
    src/ui/display_profile.cpp
    src/ui/key_map.cpp
    src/ui/render.cpp
    src/ui/ui.cpp
    src/ui/widgets.cpp
    src/ui/pages/page_help.cpp
    src/ui/pages/page_index.cpp
    src/ui/pages/page_setup.cpp
    src/ui/pages/page_testcard.cpp
)
target_include_directories(phosphor_ui PUBLIC src)
target_link_libraries(phosphor_ui PUBLIC phosphor_core PRIVATE stb)
phos_set_warnings(phosphor_ui)

# ---------------------------------------------------------------------------
# App: platform layer + executable
# ---------------------------------------------------------------------------
if(PHOS_BUILD_APP)
    add_library(phosphor_platform STATIC
        src/platform/crt_renderer_gl.cpp
        src/platform/paths.cpp
        src/platform/window_glfw.cpp
    )
    target_include_directories(phosphor_platform PUBLIC src)
    target_link_libraries(phosphor_platform PUBLIC phosphor_ui glfw glad)
    phos_set_warnings(phosphor_platform)
    if(APPLE)
        target_compile_definitions(phosphor_platform PRIVATE GL_SILENCE_DEPRECATION)
        target_link_libraries(phosphor_platform PUBLIC "-framework OpenGL" "-framework Cocoa" "-framework IOKit")
    endif()

    add_executable(phosphor
        src/app/main.cpp
        src/app/app.cpp
        src/app/cli.cpp
        src/app/headless.cpp
    )
    target_link_libraries(phosphor PRIVATE phosphor_platform)
    phos_set_warnings(phosphor)

    # Runtime files resolve next to the executable (exeDir()), independent of the CWD.
    add_custom_command(TARGET phosphor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/shaders $<TARGET_FILE_DIR:phosphor>/shaders
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:phosphor>/assets
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/conf $<TARGET_FILE_DIR:phosphor>/conf
    )
endif()

# ---------------------------------------------------------------------------
# Tools
# ---------------------------------------------------------------------------
# fontpack: converts the human-editable glyph sources in assets/fonts/src/ into
# the PNG atlas + metrics JSON the app loads. Run via the `fonts` target.
add_executable(fontpack tools/fontpack/fontpack.cpp)
target_link_libraries(fontpack PRIVATE phosphor_ui stb)
phos_set_warnings(fontpack)
add_custom_target(fonts
    COMMAND fontpack ${PHOS_ASSET_DIR}/fonts/src/phosphor_8x12.txt ${PHOS_ASSET_DIR}/fonts/phosphor_8x12
    COMMAND fontpack ${PHOS_ASSET_DIR}/fonts/src/phosphor_8x8.txt ${PHOS_ASSET_DIR}/fonts/phosphor_8x8
    DEPENDS fontpack
    COMMENT "Packing bitmap fonts into assets/fonts/"
    VERBATIM)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(PHOS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# clang-format
# ---------------------------------------------------------------------------
find_program(CLANG_FORMAT_EXE NAMES clang-format)
if(CLANG_FORMAT_EXE)
    file(GLOB_RECURSE PHOS_FORMAT_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_SOURCE_DIR}/src/*.cpp ${CMAKE_SOURCE_DIR}/src/*.h
        ${CMAKE_SOURCE_DIR}/tests/*.cpp ${CMAKE_SOURCE_DIR}/tests/*.h
        ${CMAKE_SOURCE_DIR}/tools/*.cpp)
    add_custom_target(format COMMAND ${CLANG_FORMAT_EXE} -i ${PHOS_FORMAT_SOURCES} COMMENT "clang-format")
else()
    add_custom_target(format COMMAND ${CMAKE_COMMAND} -E echo "clang-format not found; install LLVM to use this target")
endif()
