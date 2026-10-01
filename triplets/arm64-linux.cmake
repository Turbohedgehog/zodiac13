# The built-in arm64-linux, but flecs is shared: plugins loaded into one process must share
# its process-wide state, as on x64-windows. Checked by z13_plugin_smoke.
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_CMAKE_SYSTEM_NAME Linux)

if(PORT STREQUAL "flecs")
  set(VCPKG_LIBRARY_LINKAGE dynamic)
endif()
