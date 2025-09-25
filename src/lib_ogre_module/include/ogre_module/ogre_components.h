#pragma once

#include <SDL2/SDL_scancode.h>

#include <ogre_module/ogre_datatypes.h>

namespace z13::ogre {

struct OgreInfo {

};

struct OgreData {
  OgreRootPtr ogre_root;
  Ogre::RenderWindow* ogre_window = nullptr;
  InputListenerPtr input_listener;
  // bool is_window_closed = false;
};

struct OgreWindowClosed {};

struct SdlInput {
  int mouse_pos_x = 0;
  int mouse_pos_y = 0;
  int relative_mouse_pos_x = 0;
  int relative_mouse_pos_y = 0;
  bool is_mouse_captured = false;
  const Uint8* keyboadr_scancodes = nullptr;
};

struct ReadEvents { };
struct PreRender { };
struct Render { };
struct PostRender { };
// struct PreRenderGui { };
// struct RenderGui { };
// struct PostRenderGui { };
struct FinalizeRender { };

}  // namespace z13::ogre
