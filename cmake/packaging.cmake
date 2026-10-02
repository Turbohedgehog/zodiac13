# The Linux package: bin/'s runtime layout (exe, z13_core, config/, modules/, assets/) plus
# the shared flecs, spdlog and fmt, found through $ORIGIN instead of the build tree's absolute RUNPATH. Only the
# "runtime" component is packaged; the SDK installs (headers, static libs) stay out.

set(Z13_BIN_DIR "${CMAKE_SOURCE_DIR}/bin")
set(Z13_PLUGINS z13_module bullet_module net_module raylib_module test_dll_module)

find_package(flecs CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)

install(TARGETS zodiac13 RUNTIME DESTINATION . COMPONENT runtime)
set_target_properties(zodiac13 PROPERTIES INSTALL_RPATH "$ORIGIN")

foreach(plugin IN LISTS Z13_PLUGINS)
    # Where the launcher looks for it: the same place as in bin/.
    get_target_property(output_dir ${plugin} LIBRARY_OUTPUT_DIRECTORY)
    file(RELATIVE_PATH plugin_dir "${Z13_BIN_DIR}" "${output_dir}")
    install(TARGETS ${plugin}
            RUNTIME DESTINATION ${plugin_dir} COMPONENT runtime
            LIBRARY DESTINATION ${plugin_dir} COMPONENT runtime)
    # flecs sits two levels up; net_module also links z13_module.
    set_target_properties(${plugin} PROPERTIES INSTALL_RPATH "$ORIGIN/../..;$ORIGIN/../z13_module")
endforeach()

install(TARGETS core RUNTIME DESTINATION . COMPONENT runtime LIBRARY DESTINATION . COMPONENT runtime)
set_target_properties(core PROPERTIES INSTALL_RPATH "$ORIGIN")
install(IMPORTED_RUNTIME_ARTIFACTS flecs::flecs spdlog::spdlog fmt::fmt DESTINATION . COMPONENT runtime)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/config/" DESTINATION config COMPONENT runtime)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/assets/" DESTINATION assets COMPONENT runtime)

# The .deb's own files outside /opt/zodiac13: the command on PATH and a menu entry.
# EXCLUDE_FROM_ALL: only CPack asks for them, a plain `cmake --install` must not touch /usr.
set(CPACK_Z13_DEB_INSTALL_DIR "/opt/zodiac13")
if(UNIX)
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/deb")
    file(CREATE_LINK "${CPACK_Z13_DEB_INSTALL_DIR}/zodiac13" "${CMAKE_BINARY_DIR}/deb/zodiac13" SYMBOLIC)
    install(FILES "${CMAKE_BINARY_DIR}/deb/zodiac13" DESTINATION /usr/bin
            COMPONENT deb EXCLUDE_FROM_ALL)
    install(FILES "${CMAKE_SOURCE_DIR}/cmake/deb/zodiac13.desktop" DESTINATION /usr/share/applications
            COMPONENT deb EXCLUDE_FROM_ALL)

    # Depends from the binaries; the shared libraries above ship in the package itself.
    get_target_property(flecs_location flecs::flecs LOCATION)
    get_filename_component(flecs_dir "${flecs_location}" DIRECTORY)
    get_target_property(z13_module_dir z13_module LIBRARY_OUTPUT_DIRECTORY)
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS_PRIVATE_DIRS "${flecs_dir};${Z13_BIN_DIR};${z13_module_dir}")
endif()

set(CPACK_GENERATOR "DEB;TGZ")
set(CPACK_INSTALL_CMAKE_PROJECTS "${CMAKE_BINARY_DIR};${PROJECT_NAME};runtime;/")
# CPACK_-prefixed, so it reaches cpack_options.cmake at packaging time.
set(CPACK_Z13_DEB_PROJECTS "${CPACK_INSTALL_CMAKE_PROJECTS};${CMAKE_BINARY_DIR};${PROJECT_NAME};deb;/")
set(CPACK_PROJECT_CONFIG_FILE "${CMAKE_SOURCE_DIR}/cmake/cpack_options.cmake")

set(CPACK_PACKAGE_VERSION_MAJOR ${Z13_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${Z13_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${Z13_VERSION_PATCH})
set(CPACK_PACKAGE_CONTACT unlinker@mail.ru)

# zodiac13-<version>-<os>-<arch>, like the Windows zip; the .deb keeps Debian's own
# zodiac13_<version>_<arch>.deb, which apt users expect.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
    set(Z13_PACKAGE_ARCH arm64)
else()
    set(Z13_PACKAGE_ARCH x64)
endif()
string(TOLOWER "${CMAKE_SYSTEM_NAME}" Z13_PACKAGE_OS)
set(CPACK_PACKAGE_FILE_NAME "${PROJECT_NAME}-${Z13_FULL_VERSION}-${Z13_PACKAGE_OS}-${Z13_PACKAGE_ARCH}")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)

include(CPack)
