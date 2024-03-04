# Android NDK toolchain wrapper for Wind.
# Configure with ANDROID_NDK set (env or cache), for example:
#   cmake --preset android-arm64 -DENGINE_HOST_ASSET_CODEGEN=/path/to/native/asset_codegen
#
# Requires the Android NDK CMake file:
#   $ANDROID_NDK/build/cmake/android.toolchain.cmake

if(NOT ANDROID_NDK AND DEFINED ENV{ANDROID_NDK})
    set(ANDROID_NDK "$ENV{ANDROID_NDK}")
endif()
if(NOT ANDROID_NDK AND DEFINED ENV{ANDROID_NDK_HOME})
    set(ANDROID_NDK "$ENV{ANDROID_NDK_HOME}")
endif()
if(NOT ANDROID_NDK AND DEFINED ENV{ANDROID_NDK_ROOT})
    set(ANDROID_NDK "$ENV{ANDROID_NDK_ROOT}")
endif()

if(NOT ANDROID_ABI)
    set(ANDROID_ABI "arm64-v8a" CACHE STRING "Android ABI")
endif()
if(NOT ANDROID_PLATFORM)
    set(ANDROID_PLATFORM "android-21" CACHE STRING "Android API level")
endif()
if(NOT ANDROID_STL)
    set(ANDROID_STL "c++_shared" CACHE STRING "Android STL")
endif()

if(NOT ANDROID_NDK)
    message(FATAL_ERROR
        "android-ndk.cmake needs ANDROID_NDK, ANDROID_NDK_HOME, or ANDROID_NDK_ROOT.\n"
        "Install the Android NDK and reconfigure. See README.md (Build for Android).")
endif()

set(_engine_ndk_toolchain "${ANDROID_NDK}/build/cmake/android.toolchain.cmake")
if(NOT EXISTS "${_engine_ndk_toolchain}")
    message(FATAL_ERROR "Android NDK toolchain not found: ${_engine_ndk_toolchain}")
endif()

include("${_engine_ndk_toolchain}")
