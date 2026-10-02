# The Linux package: bin/'s runtime layout (exe, config/, modules/, assets/) plus the shared
# flecs, found through $ORIGIN instead of the build tree's absolute RUNPATH. Only the
# "runtime" component is packaged; the SDK installs (headers, static libs) stay out.

set(Z13_BIN_DIR "${CMAKE_SOURCE_DIR}/bin")
set(Z13_PLUGINS z13_module bullet_module net_module raylib_module test_dll_module)

find_package(flecs CONFIG REQUIRED)

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

install(IMPORTED_RUNTIME_ARTIFACTS flecs::flecs DESTINATION . COMPONENT runtime)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/config/" DESTINATION config COMPONENT runtime)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/assets/" DESTINATION assets COMPONENT runtime)

set(CPACK_GENERATOR "DEB;TGZ")
set(CPACK_INSTALL_CMAKE_PROJECTS "${CMAKE_BINARY_DIR};${PROJECT_NAME};runtime;/")
set(CPACK_PROJECT_CONFIG_FILE "${CMAKE_SOURCE_DIR}/cmake/cpack_options.cmake")

set(CPACK_PACKAGE_VERSION_MAJOR ${Z13_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${Z13_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${Z13_VERSION_PATCH})
set(CPACK_PACKAGE_FILE_NAME "${PROJECT_NAME}-${Z13_FULL_VERSION}")
set(CPACK_PACKAGE_CONTACT unlinker@mail.ru)

# Keeps arm64 artifacts from colliding with the x64 ones (whose names stay as they were).
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
    set(CPACK_PACKAGE_FILE_NAME "${PROJECT_NAME}-${Z13_FULL_VERSION}-arm64")
endif()

include(CPack)
