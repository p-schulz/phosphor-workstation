include(Catch)

set(PHOS_GOLDEN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/golden/images)

add_executable(phosphor_tests
    unit/test_canvas.cpp
    unit/test_chip.cpp
    unit/test_commands.cpp
    unit/test_display_profile.cpp
    unit/test_dsp.cpp
    unit/test_engine.cpp
    unit/test_font.cpp
    unit/test_key_map.cpp
    unit/test_library.cpp
    unit/test_m8.cpp
    unit/test_midi.cpp
    unit/test_realtime.cpp
    unit/test_sampler.cpp
    unit/test_sequencer.cpp
    unit/test_settings.cpp
    unit/test_synthesis.cpp
    unit/test_ui.cpp
    unit/test_vapor.cpp
    unit/test_viewport.cpp
    unit/test_wav.cpp
    unit/test_wave_slices.cpp
    golden/test_golden.cpp
)
target_include_directories(phosphor_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(phosphor_tests PRIVATE phosphor_core phosphor_ui Catch2::Catch2WithMain)
target_compile_definitions(phosphor_tests PRIVATE
    PHOS_ASSET_DIR="${PHOS_ASSET_DIR}"
    PHOS_GOLDEN_DIR="${PHOS_GOLDEN_DIR}"
    PHOS_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    PHOS_EXAMPLES_DIR="${CMAKE_SOURCE_DIR}/examples")
phos_set_warnings(phosphor_tests)
# libremidi's SMF reader cross-checks the SMF writer (only where the app, and so libremidi, is built).
if(TARGET libremidi)
    target_link_libraries(phosphor_tests PRIVATE libremidi)
    target_compile_definitions(phosphor_tests PRIVATE PHOS_HAVE_LIBREMIDI=1)
endif()
catch_discover_tests(phosphor_tests DISCOVERY_MODE PRE_TEST)

# Golden images: rewritten only on purpose (`cmake --build <preset> --target update_goldens`).
add_executable(golden_update golden/update_goldens.cpp)
target_include_directories(golden_update PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(golden_update PRIVATE phosphor_ui)
target_compile_definitions(golden_update PRIVATE PHOS_ASSET_DIR="${PHOS_ASSET_DIR}"
    PHOS_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
phos_set_warnings(golden_update)
add_custom_target(update_goldens
    COMMAND golden_update ${PHOS_GOLDEN_DIR}
    DEPENDS golden_update
    COMMENT "Rewriting golden page renders in tests/golden/images"
    VERBATIM)
