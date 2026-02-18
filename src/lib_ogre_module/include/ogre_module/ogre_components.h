#pragma once

#include <SDL2/SDL_scancode.h>

#include <ogre_module/ogre_datatypes.h>

struct SDL_Window;

namespace z13::ogre {
struct OgreData {
  OgreRootPtr ogre_root;
  Ogre::RenderWindow* ogre_window = nullptr;
  InputListenerPtr input_listener;
  SDL_Window* sdl_window = nullptr;
};

struct OgreSceneNode {
  SceneNodeHolderWeakPtr scene_node;
};

struct OgreWindowClosed {};

struct ReadEvents { };
struct PreRender { };
struct Render { };
struct PostRender { };
struct FinalizeRender { };

}  // namespace z13::ogre
