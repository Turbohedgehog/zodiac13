// z13.ogre.components module interface unit.
// Внутренний модуль DLL lib_ogre_module.
// Объединяет типы из src/private_ogre_components.h (SceneNodeComponent, EntityComponent)
// и include/ogre_module/ogre_components.h (OgreData, OgreSceneNode, OgreWindowClosed,
// события рендера ReadEvents/PreRender/Render/PostRender/FinalizeRender).

module;

#include <Ogre.h>
#include <SDL2/SDL_video.h>
#include <SDL2/SDL_scancode.h>
#include "ogre_tools/ogre_import/OgreImGuiInputListener.h"
// #include <OgrePrerequisites.h>

export module z13.ogre.components;

// Единый механизм импорта стандартной библиотеки.
// export import std; — потому что экспортируемые здесь типы (OgreData,
// OgreRootPtr) используют std::shared_ptr, и они должны быть видимы импортёрам.
export import std;

import z13.ogre.scene_node_holder;

// namespace OgreBites::z13 {

// struct ImGuiInputListener;

// }

export namespace z13::ogre {

using OgreRootPtr = std::shared_ptr<Ogre::Root>;
using InputListenerPtr = std::shared_ptr<OgreBites::z13::ImGuiInputListener>;

// Из src/private_ogre_components.h
struct SceneNodeComponent {
  Ogre::SceneNode* scene_node = nullptr;
};

struct EntityComponent {
  Ogre::Entity* entity = nullptr;
};

// Из include/ogre_module/ogre_components.h
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