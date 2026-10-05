# Z13_PLUGINS: every shared library that builds into bin/modules/ — the plugins the
# launcher loads at runtime. Include after all add_subdirectory() calls.

set(Z13_BIN_DIR "${CMAKE_SOURCE_DIR}/bin")

function(z13_collect_plugins directory out_var)
    set(plugins ${${out_var}})
    get_property(targets DIRECTORY ${directory} PROPERTY BUILDSYSTEM_TARGETS)
    foreach(target IN LISTS targets)
        get_target_property(type ${target} TYPE)
        get_target_property(output_dir ${target} LIBRARY_OUTPUT_DIRECTORY)
        if(type STREQUAL "SHARED_LIBRARY" AND output_dir MATCHES "^${Z13_BIN_DIR}/modules/")
            list(APPEND plugins ${target})
        endif()
    endforeach()
    get_property(subdirectories DIRECTORY ${directory} PROPERTY SUBDIRECTORIES)
    foreach(subdirectory IN LISTS subdirectories)
        z13_collect_plugins(${subdirectory} plugins)
    endforeach()
    set(${out_var} ${plugins} PARENT_SCOPE)
endfunction()

z13_collect_plugins(${CMAKE_SOURCE_DIR} Z13_PLUGINS)
