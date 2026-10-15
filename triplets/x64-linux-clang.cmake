# x64-linux built by clang with libc++, the compiler and standard library of the Android NDK:
# CI compares this build's simulation state with the GCC ones (docs/android-plan.md, stage 11).
# The CI job sets CC and CXX to the clang it installs.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_CMAKE_SYSTEM_NAME Linux)
set(VCPKG_ENV_PASSTHROUGH CC CXX)
set(VCPKG_C_FLAGS "")
set(VCPKG_CXX_FLAGS "-stdlib=libc++")
set(VCPKG_LINKER_FLAGS "-stdlib=libc++")

if(PORT MATCHES "^(flecs|spdlog|fmt)$")
  set(VCPKG_LIBRARY_LINKAGE dynamic)
endif()
