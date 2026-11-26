# Third-party dependencies, pinned to release tags (commit for stb, which has no tags).
# Keep docs/deps.md and THIRD_PARTY_LICENSES.md in sync with this file.
include(FetchContent)
set(FETCHCONTENT_QUIET ON)

# nlohmann/json — MIT
FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)

# stb (stb_image, stb_image_write) — public domain / MIT
FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG 6b5799bae8a1fe3792691ef4b72ae26434c3e799)
FetchContent_MakeAvailable(stb)
add_library(stb STATIC ${CMAKE_SOURCE_DIR}/thirdparty/stb_impl.c)
target_include_directories(stb SYSTEM PUBLIC ${stb_SOURCE_DIR})

# miniaudio — Public Domain (Unlicense) or MIT-0; stb_vorbis (bundled in its extras/) — MIT or Public Domain. Single header; its own CMakeLists is skipped
# (SOURCE_SUBDIR points at a directory that does not exist) and the implementation is compiled
# from thirdparty/miniaudio_impl.c.
FetchContent_Declare(miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG 0.11.26
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR phos-header-only)
FetchContent_MakeAvailable(miniaudio)
add_library(miniaudio STATIC ${CMAKE_SOURCE_DIR}/thirdparty/miniaudio_impl.c)
target_include_directories(miniaudio SYSTEM PUBLIC ${miniaudio_SOURCE_DIR})
# Decoders (WAV/AIFF via dr_wav, FLAC, MP3, Ogg Vorbis via the bundled extras/stb_vorbis.c) are used
# by sample import in phosphor_core; device I/O by phosphor_platform.
target_compile_definitions(miniaudio PUBLIC MA_NO_ENCODING MA_NO_GENERATION MA_NO_RESOURCE_MANAGER
                                            MA_NO_NODE_GRAPH MA_NO_ENGINE)
if(APPLE)
    target_link_libraries(miniaudio PUBLIC "-framework CoreAudio" "-framework AudioToolbox"
                                           "-framework CoreFoundation")
endif()

# r8brain-free-src — MIT (built-in Ooura FFT: permissive, see THIRD_PARTY_LICENSES.md). High-quality
# offline resampling for sample import. No CMakeLists upstream: compiled here.
FetchContent_Declare(r8brain
    GIT_REPOSITORY https://github.com/avaneev/r8brain-free-src.git
    GIT_TAG version-6.5
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(r8brain)
add_library(r8brain STATIC ${r8brain_SOURCE_DIR}/r8bbase.cpp)
target_include_directories(r8brain SYSTEM PUBLIC ${r8brain_SOURCE_DIR})
set_target_properties(r8brain PROPERTIES CXX_STANDARD 20)

# Signalsmith Stretch — MIT, with Signalsmith Linear (MIT: FFT / STFT; Apple's Accelerate framework on
# macOS). Time-stretch for loop STRETCH mode (M7), used offline on a worker thread only. Linear is
# declared first so Stretch's own CMakeLists does not fetch a second copy; no submodules (Stretch's
# cmd/util is the command-line demo). Header-only; included as SYSTEM headers.
FetchContent_Declare(signalsmith-linear
    GIT_REPOSITORY https://github.com/Signalsmith-Audio/linear.git
    GIT_TAG 0.6.4
    GIT_SHALLOW TRUE
    GIT_SUBMODULES ""
    SYSTEM)
FetchContent_Declare(signalsmith-stretch
    GIT_REPOSITORY https://github.com/Signalsmith-Audio/signalsmith-stretch.git
    GIT_TAG 1.4.0
    GIT_SHALLOW TRUE
    GIT_SUBMODULES ""
    SYSTEM)
FetchContent_MakeAvailable(signalsmith-linear signalsmith-stretch)

if(PHOS_BUILD_APP)
    # GLFW — zlib
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG 3.5.1
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(glfw)

    # libremidi — BSD-2-Clause (parts derived from RtMidi, MIT, and ModernMidi, BSD-2-Clause).
    # Static library, MIDI 1 backends only: CoreMIDI on macOS, WinMM on Windows (Windows MIDI
    # Services / UWP need downloaded NuGet packages; revisit in M9). No Boost, no JACK, PipeWire,
    # network or computer-keyboard backends.
    set(LIBREMIDI_NO_BOOST ON CACHE BOOL "" FORCE)
    set(LIBREMIDI_NO_JACK ON CACHE BOOL "" FORCE)
    set(LIBREMIDI_NO_PIPEWIRE ON CACHE BOOL "" FORCE)
    set(LIBREMIDI_NO_NETWORK ON CACHE BOOL "" FORCE)
    set(LIBREMIDI_NO_KEYBOARD ON CACHE BOOL "" FORCE)
    set(LIBREMIDI_NO_WINUWP ON CACHE BOOL "" FORCE)
    set(LIBREMIDI_NO_WINMIDI ON CACHE BOOL "" FORCE)
    set(LIBREMIDI_DOWNLOAD_CPPWINRT OFF CACHE BOOL "" FORCE)
    set(LIBREMIDI_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(LIBREMIDI_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(libremidi
        GIT_REPOSITORY https://github.com/celtera/libremidi.git
        GIT_TAG v5.4.3
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(libremidi)

    # glad — generated once with glad2 2.0.8 (gl:core=3.3, no extensions), vendored.
    add_library(glad STATIC ${CMAKE_SOURCE_DIR}/thirdparty/glad/src/gl.c)
    target_include_directories(glad SYSTEM PUBLIC ${CMAKE_SOURCE_DIR}/thirdparty/glad/include)
endif()

if(PHOS_BUILD_TESTS)
    # Catch2 v3 — BSL-1.0
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.16.1
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
endif()
