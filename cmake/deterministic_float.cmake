# Deterministic floating point (docs/tech/architecture/Principles.md, Deterministic simulation; docs/tech/build/CMake.md).
# Included by the root CMakeLists.txt, before any target, and by wind_game.cmake, in the source build and the SDK.
#
# engine_deterministic_float_options(<out_var>) sets <out_var> to the compile options every translation unit that
# compiles engine or glm code takes: no contraction to fused multiply-add and no fast-math. They are the same for the
# engine, its tests and tools, and every game target, because inline engine and glm code compiled by a game with other
# flags would be a second definition of the same function (ODR), and the linker may keep either one.
#
# It also fails the configure for a 32-bit target other than WebAssembly (x87, and ARMv7 NEON, which flushes denormals)
# and for flags that turn contraction or fast-math back on.

function(engine_deterministic_float_options out_var)
    if(CMAKE_SIZEOF_VOID_P EQUAL 4 AND NOT EMSCRIPTEN)
        message(FATAL_ERROR
            "Wind needs a 64-bit target (or WebAssembly): 32-bit x86 and ARM break deterministic simulation "
            "(docs/tech/architecture/Principles.md).")
    endif()
    string(TOUPPER "${CMAKE_BUILD_TYPE}" _engine_build_type)
    set(_engine_flags "${CMAKE_CXX_FLAGS} ${CMAKE_C_FLAGS}")
    foreach(_engine_config IN LISTS CMAKE_CONFIGURATION_TYPES _engine_build_type)
        string(TOUPPER "${_engine_config}" _engine_config)
        string(APPEND _engine_flags " ${CMAKE_CXX_FLAGS_${_engine_config}} ${CMAKE_C_FLAGS_${_engine_config}}")
    endforeach()
    foreach(_engine_bad IN ITEMS "/fp:fast" "-fp:fast" "/fp:contract" "-fp:contract" "-ffast-math" "-Ofast"
            "-ffp-contract=fast" "-ffp-contract=on" "-funsafe-math-optimizations" "-mfma")
        string(FIND "${_engine_flags}" "${_engine_bad}" _engine_at)
        if(NOT _engine_at EQUAL -1)
            message(FATAL_ERROR
                "\"${_engine_bad}\" in the compiler flags breaks deterministic simulation "
                "(docs/tech/architecture/Principles.md). Remove it.")
        endif()
    endforeach()

    if(MSVC AND CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        # /fp:precise does not contract since Visual Studio 2022 (contraction is /fp:contract, never passed).
        set(_engine_options /fp:precise)
    elseif(MSVC)
        # clang-cl: its /fp:precise allows contraction within an expression, so turn it off after.
        set(_engine_options /fp:precise /clang:-ffp-contract=off)
    else()
        set(_engine_options -ffp-contract=off -fno-fast-math)
    endif()
    set(_engine_result "")
    foreach(_engine_option IN LISTS _engine_options)
        list(APPEND _engine_result "$<$<COMPILE_LANGUAGE:C,CXX>:${_engine_option}>")
    endforeach()
    set(${out_var} "${_engine_result}" PARENT_SCOPE)
endfunction()
