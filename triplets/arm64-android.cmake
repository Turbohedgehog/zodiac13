# The built-in arm64-android on Android 8 (API 26, docs/android-plan.md), with the libraries
# of arm64-linux shared for the same reason. SDL3 is shared too: SDLActivity enters through
# its JNI functions, and the app (libmain.so) and raylib_module must see one SDL.
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_CMAKE_SYSTEM_NAME Android)
set(VCPKG_CMAKE_SYSTEM_VERSION 26)
set(VCPKG_MAKE_BUILD_TRIPLET "--host=aarch64-linux-android")
set(VCPKG_CMAKE_CONFIGURE_OPTIONS -DANDROID_ABI=arm64-v8a)

if(PORT MATCHES "^(flecs|spdlog|fmt|sdl3)$")
  set(VCPKG_LIBRARY_LINKAGE dynamic)
endif()
