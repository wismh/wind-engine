# wind_ui_bench: the UI bench (ENGINE_BENCH only). Renders one scene in one mode, records the UI profiler, writes the
# snapshot with the run's metadata, and exits. Built against the static engine like wind_launcher; reference numbers
# come from RelWithDebInfo (the profiler is compiled in Debug and RelWithDebInfo). docs/tech/features/UI Bench.md

# The command line, matrix, schedule, frame plan, data, report, and hover-point logic, so wind_ui_bench_tests compiles
# the same files without a window or the scene assets.
set(_wind_ui_bench_parts
    src/bench_matrix.cpp
    src/bench_names.cpp
    src/bench_options.cpp
    src/bench_random.cpp
    src/bench_report.cpp
    src/bench_schedule.cpp
    src/frame_plan.cpp
    src/hud_data.cpp
    src/scene_points.cpp
    src/table_data.cpp
    src/text_data.cpp)

add_executable(wind_ui_bench
    src/main.cpp
    src/arc_paint.cpp
    src/bench_app.cpp
    src/bench_assets.cpp
    src/bool_row_view_model.cpp
    src/cell_row_view_model.cpp
    src/cell_view_model.cpp
    src/clip_scene.cpp
    src/clip_view_model.cpp
    src/group_view_model.cpp
    src/hud_scene.cpp
    src/hud_view_model.cpp
    src/inspector_scene.cpp
    src/inspector_view_model.cpp
    src/minimap_paint.cpp
    src/motion_scene.cpp
    src/motion_view_model.cpp
    src/number_row_view_model.cpp
    src/paint_mix_scene.cpp
    src/paint_mix_view_model.cpp
    src/pane_row_view_model.cpp
    src/pane_view_model.cpp
    src/paragraph_view_model.cpp
    src/scene_factory.cpp
    src/section_view_model.cpp
    src/table_row_view_model.cpp
    src/table_scene.cpp
    src/table_view_model.cpp
    src/text_row_view_model.cpp
    src/text_scene.cpp
    src/text_view_model.cpp
    src/unit_row_view_model.cpp
    src/world_paint.cpp
    ${_wind_ui_bench_parts})
engine_configure_app(wind_ui_bench)
target_include_directories(wind_ui_bench PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
if(MSVC)
    # A console program in every configuration: it prints --list and its result, and is run from a shell.
    # engine_configure_app gives non-Debug executables /SUBSYSTEM:WINDOWS; the last /SUBSYSTEM wins.
    target_link_options(wind_ui_bench PRIVATE "/SUBSYSTEM:CONSOLE")
endif()

# What the report records about the build. The commit is read once, at configure time, and is empty without git.
set(_wind_ui_bench_commit "")
set(_wind_ui_bench_dirty 0)
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        OUTPUT_VARIABLE _wind_ui_bench_commit
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _wind_ui_bench_git_result
        ERROR_QUIET)
    if(NOT _wind_ui_bench_git_result EQUAL 0)
        set(_wind_ui_bench_commit "")
    else()
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=no
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
            OUTPUT_VARIABLE _wind_ui_bench_status
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
        if(NOT _wind_ui_bench_status STREQUAL "")
            set(_wind_ui_bench_dirty 1)
        endif()
    endif()
endif()
target_compile_definitions(wind_ui_bench PRIVATE
    "WIND_BENCH_CONFIG=\"$<CONFIG>\""
    "WIND_BENCH_ENGINE_VERSION=\"${PROJECT_VERSION}\""
    "WIND_BENCH_COMMIT=\"${_wind_ui_bench_commit}\""
    "WIND_BENCH_DIRTY=${_wind_ui_bench_dirty}")

if(ENGINE_BUILD_TESTS)
    add_executable(wind_ui_bench_tests tests/bench_test.cpp ${_wind_ui_bench_parts})
    set_target_properties(wind_ui_bench_tests PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF)
    if(MSVC)
        target_compile_options(wind_ui_bench_tests PRIVATE /W4 /permissive- /utf-8)
    endif()
    target_link_libraries(wind_ui_bench_tests PRIVATE engine GTest::gtest_main)
    target_include_directories(wind_ui_bench_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    # The scene assets: a test parses every stylesheet and document.
    target_compile_definitions(wind_ui_bench_tests PRIVATE WIND_BENCH_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
    gtest_discover_tests(wind_ui_bench_tests DISCOVERY_MODE PRE_TEST)
endif()
