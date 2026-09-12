cmake_minimum_required(VERSION 3.21)
project(sdl_raylib_cube CXX)

set(CMAKE_CXX_STANDARD 17)

set(VENDOR ${CMAKE_CURRENT_SOURCE_DIR}/vendor)
set(IMGUI ${VENDOR}/imgui)

set(IMGUI_SOURCES
  ${IMGUI}/imgui.cpp
  ${IMGUI}/imgui_draw.cpp
  ${IMGUI}/imgui_tables.cpp
  ${IMGUI}/imgui_widgets.cpp
  ${IMGUI}/imgui_demo.cpp
  ${IMGUI}/backends/imgui_impl_sdl2.cpp
  ${IMGUI}/backends/imgui_impl_opengl3.cpp
)

# One target per raylib version, same main.cpp, so behaviour can be compared 1:1.
function(add_cube_target name raylib_dir)
  add_executable(${name} main.cpp ${IMGUI_SOURCES})
  target_include_directories(${name} PRIVATE
    ${raylib_dir}/include
    ${VENDOR}/SDL2/include
    ${IMGUI}
  )
  target_link_libraries(${name} PRIVATE
    ${raylib_dir}/lib/raylibdll.lib    # import lib for the self-contained raylib.dll
    ${VENDOR}/SDL2/lib/x64/SDL2.lib
    ${VENDOR}/SDL2/lib/x64/SDL2main.lib
    opengl32
  )
  # Each exe gets its own dir so its raylib.dll doesn't clobber the other's.
  set_target_properties(${name} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${name}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/${name}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/${name}
  )
  if(MSVC)
    target_compile_definitions(${name} PRIVATE _CRT_SECURE_NO_WARNINGS)
    set_target_properties(${name} PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")
  endif()
  add_custom_command(TARGET ${name} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${raylib_dir}/lib/raylib.dll
            ${VENDOR}/SDL2/lib/x64/SDL2.dll
            $<TARGET_FILE_DIR:${name}>
  )
endfunction()

add_cube_target(sdl_raylib_cube   ${VENDOR}/raylib)     # raylib 5.5
if(EXISTS ${VENDOR}/raylib60/lib/raylibdll.lib)
  add_cube_target(sdl_raylib_cube60 ${VENDOR}/raylib60) # raylib 6.0
endif()
