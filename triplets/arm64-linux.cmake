# The built-in arm64-linux, but flecs, spdlog and fmt are shared, as on x64-windows: flecs and
# spdlog keep process-wide state every plugin must share, and one copy of each beats one
# per plugin. Checked by z13_plugin_smoke.
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_CMAKE_SYSTEM_NAME Linux)

if(PORT MATCHES "^(flecs|spdlog|fmt)$")
  set(VCPKG_LIBRARY_LINKAGE dynamic)
endif()
