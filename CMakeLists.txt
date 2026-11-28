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
option(PHOS_NATIVE "Tune for the CPU of the build machine (builds on the device, e.g. a Raspberry Pi)" OFF)

# Set before the dependencies so they are tuned too. Not for builds that run on other machines.
if(PHOS_NATIVE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT APPLE)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|armv")
        add_compile_options(-mcpu=native)
    else()
        add_compile_options(-march=native)
    endif()
endif()

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)
include(Warnings)
include(Dependencies)

set(PHOS_ASSET_DIR ${CMAKE_SOURCE_DIR}/assets)

# ---------------------------------------------------------------------------
# phosphor_core — core, dsp, synth, seq, model, engine, io. No GL, no device I/O.
# ---------------------------------------------------------------------------
add_library(phosphor_core STATIC
    src/core/notes.cpp
    src/core/rt_guard.cpp
    src/core/text.cpp
    src/core/version.cpp
    src/dsp/analysis.cpp
    src/dsp/fx_basic.cpp
    src/dsp/fx_mod.cpp
    src/dsp/plate.cpp
    src/dsp/gate_reverb.cpp
    src/dsp/limiter.cpp
    src/dsp/stretch.cpp
    src/dsp/yin.cpp
    src/engine/engine.cpp
    src/engine/engine_midi.cpp
    src/engine/engine_midi_out.cpp
    src/engine/engine_mix.cpp
    src/engine/loop_player.cpp
    src/engine/snapshot_builder.cpp
    src/engine/offline.cpp
    src/content/gm.cpp
    src/content/presets.cpp
    src/content/templates.cpp
    src/engine/gb_voice.cpp
    src/engine/drum_voice.cpp
    src/engine/sampler_voice.cpp
    src/engine/snes_bus.cpp
    src/engine/snes_voice.cpp
    src/engine/synth_voice.cpp
    src/engine/wave_voice.cpp
    src/engine/wavetables.cpp
    src/model/song.cpp
    src/model/wavetable.cpp
    src/model/wave_slices.cpp
    src/seq/compiled_song.cpp
    src/seq/sequencer.cpp
    src/seq/timing.cpp
    src/engine/voice_pool.cpp
    src/io/import.cpp
    src/io/instrument_io.cpp
    src/io/project.cpp
    src/io/project_song.cpp
    src/io/render_export.cpp
    src/io/smf.cpp
    src/io/snes.cpp
    src/io/wav.cpp
    src/model/chip.cpp
    src/model/fx.cpp
    src/model/sample_data.cpp
    src/model/workspace.cpp
)
target_include_directories(phosphor_core PUBLIC src)
target_compile_definitions(phosphor_core PUBLIC
    PHOS_VERSION_STRING="${PROJECT_VERSION}")
target_link_libraries(phosphor_core PUBLIC nlohmann_json::nlohmann_json Threads::Threads PRIVATE miniaudio r8brain signalsmith-stretch)
# The STRETCH cache must render fast in Debug too (PLAN.md M7 AC: 8-bar loop < 2 s).
if(NOT MSVC)
    set_source_files_properties(src/dsp/stretch.cpp PROPERTIES COMPILE_OPTIONS "-O2")
endif()
# Realtime allocation guard (src/core/rt_guard.cpp): replaces global new/delete in Debug builds.
target_compile_definitions(phosphor_core PUBLIC $<$<CONFIG:Debug>:PHOS_RT_GUARD=1>)
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
    src/ui/mix_commands.cpp
    src/ui/render.cpp
    src/ui/settings.cpp
    src/ui/song_commands.cpp
    src/ui/ui.cpp
    src/ui/ui_commands.cpp
    src/ui/side_keys.cpp
    src/ui/widgets.cpp
    src/ui/pages/page_chip.cpp
    src/ui/pages/page_chip_gb.cpp
    src/ui/pages/page_chip_snes.cpp
    src/ui/pages/page_composer.cpp
    src/ui/pages/page_composer_draw.cpp
    src/ui/pages/page_control.cpp
    src/ui/pages/page_disk.cpp
    src/ui/pages/page_harmonics.cpp
    src/ui/pages/page_help.cpp
    src/ui/pages/page_index.cpp
    src/ui/pages/page_keyboard.cpp
    src/ui/pages/page_loops.cpp
    src/ui/pages/midi_out_view.cpp
    src/ui/pages/automation_view.cpp
    src/ui/pages/page_mixer.cpp
    src/ui/pages/page_sample.cpp
    src/ui/pages/page_spectral.cpp
    src/ui/pages/page_synth.cpp
    src/ui/pages/page_setup.cpp
    src/ui/pages/page_testcard.cpp
    src/ui/pages/page_vkeys.cpp
    src/ui/pages/page_wavedraw.cpp
    src/ui/pages/page_waveform.cpp
    src/ui/pages/waterfall.cpp
)
target_include_directories(phosphor_ui PUBLIC src)
target_link_libraries(phosphor_ui PUBLIC phosphor_core PRIVATE stb)
phos_set_warnings(phosphor_ui)

# ---------------------------------------------------------------------------
# App: platform layer + executable
# ---------------------------------------------------------------------------
if(PHOS_BUILD_APP)
    add_library(phosphor_platform STATIC
        src/platform/audio_miniaudio.cpp
        src/platform/crt_renderer_gl.cpp
        src/platform/midi_libremidi.cpp
        src/platform/midi_out_thread.cpp
        src/platform/paths.cpp
        src/platform/window_glfw.cpp
    )
    target_include_directories(phosphor_platform PUBLIC src)
    target_link_libraries(phosphor_platform PUBLIC phosphor_ui glfw glad PRIVATE miniaudio libremidi)
    phos_set_warnings(phosphor_platform)
    if(APPLE)
        target_compile_definitions(phosphor_platform PRIVATE GL_SILENCE_DEPRECATION)
        target_link_libraries(phosphor_platform PUBLIC "-framework OpenGL" "-framework Cocoa" "-framework IOKit")
    endif()

    add_executable(phosphor
        src/app/main.cpp
        src/app/app.cpp
        src/app/app_audio.cpp
        src/app/app_files.cpp
        src/app/app_loops.cpp
        src/app/app_midi.cpp
        src/app/app_samples.cpp
        src/app/app_song.cpp
        src/app/cli.cpp
        src/app/headless.cpp
        src/app/tools.cpp
    )
    target_link_libraries(phosphor PRIVATE phosphor_platform)
    phos_set_warnings(phosphor)

    # midi_probe: virtual MIDI source for burst / latency tests (tools/midi_probe).
    add_executable(midi_probe tools/midi_probe/midi_probe.cpp)
    target_link_libraries(midi_probe PRIVATE libremidi)
    phos_set_warnings(midi_probe)

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
# fixturegen: writes the WAV fixtures for import tests (tests/fixtures/make_fixtures.sh).
add_executable(fixturegen tools/fixturegen/fixturegen.cpp)
phos_set_warnings(fixturegen)

# examplegen: writes the example instrument library and projects into examples/ (`examples` target).
add_executable(examplegen tools/examplegen/examplegen.cpp)
target_link_libraries(examplegen PRIVATE phosphor_core)
phos_set_warnings(examplegen)
add_custom_target(examples
    COMMAND examplegen ${CMAKE_SOURCE_DIR}/examples
    DEPENDS examplegen
    COMMENT "Writing examples/library, examples/projects, examples/midi"
    VERBATIM)

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
