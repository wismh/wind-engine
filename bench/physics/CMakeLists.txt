add_executable(wind_physics_bench
    main.cpp
    bench_2d.cpp
    bench_static_2d.cpp
    bench_3d.cpp
)

set_target_properties(wind_physics_bench PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

if(MSVC)
    target_compile_options(wind_physics_bench PRIVATE /W4 /permissive- /utf-8)
    target_link_options(wind_physics_bench PRIVATE "/SUBSYSTEM:CONSOLE")
endif()

target_link_libraries(wind_physics_bench PRIVATE engine)
target_include_directories(wind_physics_bench PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
