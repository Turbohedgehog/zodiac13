# Android: every plugin is lib<name>.so (the APK keeps only such names, see ModuleLibraryPath),
# and z13_android_stage puts what the Gradle project in android/ packs into
# ${Z13_ANDROID_STAGE_DIR}: jniLibs/arm64-v8a/ and assets/.

set(Z13_ANDROID_STAGE_DIR "${CMAKE_BINARY_DIR}/android")
set(Z13_ANDROID_LIBS_DIR "${Z13_ANDROID_STAGE_DIR}/jniLibs/${ANDROID_ABI}")

set_target_properties(${Z13_PLUGINS} PROPERTIES PREFIX "lib")

# vcpkg installs during configure; the only shared libraries are the triplet's.
file(GLOB z13_vcpkg_shared_libs "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib/*.so")
set(z13_cxx_runtime "${CMAKE_SYSROOT}/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}/libc++_shared.so")
if(NOT EXISTS "${z13_cxx_runtime}")
    message(FATAL_ERROR "libc++_shared.so not found at ${z13_cxx_runtime}")
endif()

set(z13_app_targets zodiac13 core ${Z13_PLUGINS})
set(z13_app_libs ${z13_app_targets})
list(TRANSFORM z13_app_libs REPLACE "^(.+)$" "$<TARGET_FILE:\\1>")
# The NDK compiles with -g even in Release: the staged copies lose it (most of the APK otherwise).
set(z13_staged_libs ${z13_app_targets})
list(TRANSFORM z13_staged_libs REPLACE "^(.+)$" "${Z13_ANDROID_LIBS_DIR}/$<TARGET_FILE_NAME:\\1>")
foreach(lib IN LISTS z13_vcpkg_shared_libs)
    get_filename_component(name "${lib}" NAME)
    list(APPEND z13_staged_libs "${Z13_ANDROID_LIBS_DIR}/${name}")
endforeach()

add_custom_target(z13_android_stage
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${Z13_ANDROID_STAGE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${Z13_ANDROID_LIBS_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy ${z13_app_libs} ${z13_vcpkg_shared_libs} "${z13_cxx_runtime}"
            "${Z13_ANDROID_LIBS_DIR}"
    COMMAND ${CMAKE_STRIP} --strip-debug ${z13_staged_libs}
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -DOUTPUT_DIR=${Z13_ANDROID_STAGE_DIR}/assets
            -P ${CMAKE_SOURCE_DIR}/cmake/android_assets.cmake
    COMMENT "Staging the APK's libraries and files in ${Z13_ANDROID_STAGE_DIR}"
    VERBATIM
)
add_dependencies(z13_android_stage zodiac13)
