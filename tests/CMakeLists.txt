include(Catch)

set(PHOS_GOLDEN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/golden/images)

add_executable(phosphor_tests
    unit/test_canvas.cpp
    unit/test_commands.cpp
    unit/test_display_profile.cpp
    unit/test_font.cpp
    unit/test_key_map.cpp
    unit/test_ui.cpp
    unit/test_viewport.cpp
    golden/test_golden.cpp
)
target_include_directories(phosphor_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(phosphor_tests PRIVATE phosphor_core phosphor_ui Catch2::Catch2WithMain)
target_compile_definitions(phosphor_tests PRIVATE
    PHOS_ASSET_DIR="${PHOS_ASSET_DIR}"
    PHOS_GOLDEN_DIR="${PHOS_GOLDEN_DIR}")
phos_set_warnings(phosphor_tests)
catch_discover_tests(phosphor_tests DISCOVERY_MODE PRE_TEST)

# Golden images: rewritten only on purpose (`cmake --build <preset> --target update_goldens`).
add_executable(golden_update golden/update_goldens.cpp)
target_include_directories(golden_update PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(golden_update PRIVATE phosphor_ui)
target_compile_definitions(golden_update PRIVATE PHOS_ASSET_DIR="${PHOS_ASSET_DIR}")
phos_set_warnings(golden_update)
add_custom_target(update_goldens
    COMMAND golden_update ${PHOS_GOLDEN_DIR}
    DEPENDS golden_update
    COMMENT "Rewriting golden page renders in tests/golden/images"
    VERBATIM)
