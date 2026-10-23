project(raylib_module)

find_package(raylib CONFIG REQUIRED)   # used only as an rlgl render lib (no InitWindow)
find_package(SDL3 CONFIG REQUIRED)     # owns the window / GL context / event loop
find_package(imgui CONFIG REQUIRED)    # UI layer (sdl3 + opengl3 bindings)
find_package(Boost REQUIRED COMPONENTS dll)
find_package(Eigen3 REQUIRED NO_MODULE)
find_package(assimp CONFIG REQUIRED)

z13_add_plugin(
  ${PROJECT_NAME} modules/raylib FBS_HEADERS
  PRECOMPILE "<flecs.h>" "<Eigen/Dense>"
  SOURCES
  src/gui/build_info.cpp
  src/gui/game_scenes.cpp
  src/gui/gui_keybindings.cpp
  src/gui/gui_system.cpp
  src/gui/graphics_settings_window.cpp
  src/gui/gui_windows.cpp
  src/gui/hotbar_hud.cpp
  src/gui/input_settings_window.cpp
  src/gui/join_window.cpp
  src/gui/key_bindings_window.cpp
  src/gui/loading_screen.cpp
  src/gui/main_menu_window.cpp
  src/gui/palette_window.cpp
  src/gui/pause_menu_window.cpp
  src/gui/profile_window.cpp
  src/gui/render_diagnostics.cpp
  src/gui/room_label.cpp
  src/gui/start_game_window.cpp
  src/gui/start_server_window.cpp
  src/gui/stats_overlay.cpp
  src/gui/touch_hud.cpp
  src/platform/gl_info.cpp
  src/platform/sdl_platform.cpp
  src/raylib_module.cpp
  src/raylib_module_factory.cpp
  src/raylib_system.cpp
  src/render/block_checker.cpp
  src/render/block_chunks.cpp
  src/render/block_chunks_state.cpp
  src/render/block_meshes.cpp
  src/render/block_meshes_state.cpp
  src/render/block_model.cpp
  src/render/block_textures.cpp
  src/render/cell_lighting.cpp
  src/render/cell_lighting_state.cpp
  src/render/debug_view.cpp
  src/render/environment_render_system.cpp
  src/render/frame_profile.cpp
  src/render/gl_depth_buffer.cpp
  src/render/gl_volumes.cpp
  src/render/lights.cpp
  src/render/render_resources.cpp
  src/render/render_tour.cpp
  src/render/shadow_maps.cpp
  src/render/shadow_maps_state.cpp
  src/render/skybox.cpp
  src/render/station_props.cpp
  src/render/light_clusters.cpp
  src/tools/assimp_loader.cpp
  src/tools/input_publisher.cpp
  src/tools/math_convert.cpp
  src/tools/shader_asset.cpp
  PRIVATE
  zodiac13::core
  zodiac13::components
  zodiac13::primitives
  zodiac13::rooms
  zodiac13::cell_light
  zodiac13::settings
  zodiac13::touch
  raylib
  SDL3::SDL3
  imgui::imgui
  Boost::dll
  Eigen3::Eigen
  assimp::assimp
)

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# raylib.h and <windows.h> both declare CloseWindow/ShowCursor/DrawText/Rectangle/...
# Drop the GDI/USER parts of windows.h (this module needs neither).
if(WIN32)
  target_compile_definitions(${PROJECT_NAME} PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX NOGDI NOUSER)
endif()

# imgui's and raylib's GL calls resolve against GLES there; rlgl.h must see the same API.
if(ANDROID)
  target_compile_definitions(${PROJECT_NAME} PRIVATE GRAPHICS_API_OPENGL_ES3)
  target_link_libraries(${PROJECT_NAME} PRIVATE GLESv3 EGL)
endif()

# vcpkg's applocal step already deploys raylib.dll / glfw3.dll / ... next to
# raylib_module.dll; the exe loads it with LOAD_WITH_ALTERED_SEARCH_PATH, so the
# whole chain resolves from bin/modules/raylib/.
