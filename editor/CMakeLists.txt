# wind_editor: the editor host (ENGINE_EDITOR only). An engine client like a game: it includes only
# <engine/...> and links engine. engine_configure_app gives it what engine_add_game gives a game
# executable: C++23, warnings, bin/ output, cooked assets/, and the runtime copy. Its own assets and
# catalog go to bin/assets/editor/, so a game module built into the same bin/ keeps bin/assets/.

set(_wind_editor_sources
    src/editor_app.cpp
    src/editor_options.cpp
    src/editor_view_model.cpp
    src/engine_host_play.cpp
    src/main.cpp
    src/play_session.cpp
    src/toolbar.cpp)

add_executable(wind_editor ${_wind_editor_sources})
set_property(TARGET wind_editor PROPERTY ENGINE_RUNTIME_ASSETS_DIR editor)
engine_configure_app(wind_editor)
target_include_directories(wind_editor PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

if(ENGINE_BUILD_TESTS)
    # PlaySession against headless services and the fixture game module (tests/fixtures/game_module).
    add_executable(wind_editor_tests
        tests/play_session_test.cpp
        src/play_session.cpp)
    set_target_properties(wind_editor_tests PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF)
    if(MSVC)
        target_compile_options(wind_editor_tests PRIVATE /W4 /permissive- /utf-8)
    endif()
    target_link_libraries(wind_editor_tests PRIVATE engine GTest::gtest_main)
    target_include_directories(wind_editor_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${ENGINE_CMAKE_DIR}/tests")
    target_compile_definitions(wind_editor_tests PRIVATE ${ENGINE_FIXTURE_GAME_DEFINES})
    add_dependencies(wind_editor_tests wind_fixture_game_ok)
    set_property(TARGET wind_editor_tests PROPERTY ENGINE_RUNTIME_ASSETS_DIR editor)
    engine_prepare_runtime(wind_editor_tests)
    gtest_discover_tests(wind_editor_tests DISCOVERY_MODE PRE_TEST)
endif()
