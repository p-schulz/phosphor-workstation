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
