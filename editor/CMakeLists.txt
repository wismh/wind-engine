# wind_editor: the editor host (ENGINE_EDITOR only). An engine client like a game: it includes only
# <engine/...> and links engine. engine_configure_app gives it what engine_add_game gives a game
# executable: C++23, warnings, bin/ output, cooked assets/, and the runtime copy. Its own assets and
# catalog go to bin/assets/editor/, so a game module built into the same bin/ keeps bin/assets/.

# Everything but main.cpp and the EditorApp shell, so wind_editor_tests compiles the same panels.
set(_wind_editor_parts
    src/asset_inspection.cpp
    src/asset_sync.cpp
    src/asset_watch.cpp
    src/batch_run.cpp
    src/build_line_view_model.cpp
    src/command_history.cpp
    src/build_panel.cpp
    src/build_view_model.cpp
    src/dock_layout_file.cpp
    src/edit_module.cpp
    src/editor_cli.cpp
    src/editor_options.cpp
    src/editor_panels.cpp
    src/editor_selection.cpp
    src/editor_view_model.cpp
    src/engine_host_play.cpp
    src/entity_edit.cpp
    src/entity_inspection.cpp
    src/export_dialog.cpp
    src/export_view_model.cpp
    src/explorer_panel.cpp
    src/explorer_row_view_model.cpp
    src/explorer_view_model.cpp
    src/hierarchy_panel.cpp
    src/hierarchy_row_view_model.cpp
    src/hierarchy_view_model.cpp
    src/inspector_field_view_model.cpp
    src/inspector_line_view_model.cpp
    src/inspector_panel.cpp
    src/inspector_section_view_model.cpp
    src/inspector_view_model.cpp
    src/native_build.cpp
    src/opening_overlay.cpp
    src/opening_view_model.cpp
    src/play_session.cpp
    src/profiler_chart_paint.cpp
    src/profiler_panel.cpp
    src/profiler_row_view_model.cpp
    src/profiler_stats.cpp
    src/profiler_view_model.cpp
    src/project_build.cpp
    src/project_check.cpp
    src/project_edit.cpp
    src/project_export.cpp
    src/project_scan.cpp
    src/toolbar.cpp
    src/ui_tree_panel.cpp
    src/ui_tree_row_view_model.cpp
    src/ui_tree_view_model.cpp
    src/world_view_panel.cpp
    src/world_view_view_model.cpp)

add_executable(wind_editor src/main.cpp src/editor_app.cpp ${_wind_editor_parts})
set_property(TARGET wind_editor PROPERTY ENGINE_RUNTIME_ASSETS_DIR editor)
engine_configure_app(wind_editor)
# Not a .app bundle on macOS (engine_configure_app makes every executable one): the SDK is bin/wind_editor with
# assets/ and libengine beside it, and the launcher starts it by that path.
set_target_properties(wind_editor PROPERTIES MACOSX_BUNDLE OFF)
target_include_directories(wind_editor PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(wind_editor PRIVATE "WIND_ENGINE_VERSION=\"${ENGINE_VERSION_FULL}\"")
find_package(Threads REQUIRED)
target_link_libraries(wind_editor PRIVATE Threads::Threads)
if(APPLE)
    target_link_libraries(wind_editor PRIVATE "-framework CoreServices")
endif()

if(ENGINE_BUILD_TESTS)
    # PlaySession against headless services and the fixture game module (tests/fixtures/game_module),
    # and the panels' view-models against headless worlds. The view-models use wind_editor's generated
    # asset_ids.h.
    add_executable(wind_editor_tests
        tests/asset_inspection_test.cpp
        tests/asset_watch_test.cpp
        tests/batch_run_test.cpp
        tests/build_panel_test.cpp
        tests/command_history_test.cpp
        tests/dock_layout_file_test.cpp
        tests/edit_module_test.cpp
        tests/editor_cli_test.cpp
        tests/editor_options_test.cpp
        tests/editor_panels_test.cpp
        tests/explorer_panel_test.cpp
        tests/export_dialog_test.cpp
        tests/hierarchy_panel_test.cpp
        tests/inspector_panel_test.cpp
        tests/native_build_test.cpp
        tests/opening_overlay_test.cpp
        tests/play_session_test.cpp
        tests/profiler_chart_test.cpp
        tests/profiler_panel_test.cpp
        tests/project_build_test.cpp
        tests/project_edit_test.cpp
        tests/project_export_test.cpp
        tests/toolbar_test.cpp
        tests/ui_tree_panel_test.cpp
        tests/world_view_panel_test.cpp
        ${_wind_editor_parts})
    set_target_properties(wind_editor_tests PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF)
    if(MSVC)
        target_compile_options(wind_editor_tests PRIVATE /W4 /permissive- /utf-8)
    endif()
    target_link_libraries(wind_editor_tests PRIVATE engine GTest::gtest_main Threads::Threads)
    if(APPLE)
        target_link_libraries(wind_editor_tests PRIVATE "-framework CoreServices")
    endif()
    target_include_directories(wind_editor_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_BINARY_DIR}/generated/wind_editor"
        "${ENGINE_CMAKE_DIR}/tests")
    add_dependencies(wind_editor_tests wind_editor_assets)
    target_compile_definitions(wind_editor_tests PRIVATE ${ENGINE_FIXTURE_GAME_DEFINES})
    add_dependencies(wind_editor_tests wind_fixture_game_ok)
    set_property(TARGET wind_editor_tests PROPERTY ENGINE_RUNTIME_ASSETS_DIR editor)
    engine_prepare_runtime(wind_editor_tests)
    gtest_discover_tests(wind_editor_tests DISCOVERY_MODE PRE_TEST)
endif()
