# wind_launcher: Wind Launcher (ENGINE_LAUNCHER only). A Wind app like a game (ENGINE_GAME), built against the static
# engine so it starts editor SDKs of every version. engine_configure_app gives it what a game executable gets: C++23,
# warnings, bin/ output, cooked assets/ beside it, and the engine's assets/engine/.

# The list, SDK, project, and template logic, so wind_launcher_tests compiles the same files without the UI.
set(_wind_launcher_parts
    src/launcher_state.cpp
    src/project_entry.cpp
    src/project_template.cpp
    src/sdk_catalog.cpp
    src/user_paths.cpp)

add_executable(wind_launcher
    src/main.cpp
    src/launcher_app.cpp
    src/launcher_view_model.cpp
    src/project_row_view_model.cpp
    src/sdk_option_view_model.cpp
    src/sdk_row_view_model.cpp
    ${_wind_launcher_parts})
engine_configure_app(wind_launcher)
target_include_directories(wind_launcher PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

if(ENGINE_BUILD_TESTS)
    add_executable(wind_launcher_tests tests/launcher_test.cpp ${_wind_launcher_parts})
    set_target_properties(wind_launcher_tests PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF)
    if(MSVC)
        target_compile_options(wind_launcher_tests PRIVATE /W4 /permissive- /utf-8)
    endif()
    target_link_libraries(wind_launcher_tests PRIVATE engine GTest::gtest_main)
    target_include_directories(wind_launcher_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    # The engine's templates/, which the SDK installs: a test makes a project from it.
    target_compile_definitions(wind_launcher_tests PRIVATE WIND_ENGINE_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    gtest_discover_tests(wind_launcher_tests DISCOVERY_MODE PRE_TEST)
endif()
