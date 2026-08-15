// z13.ogre.components module interface unit.
// Внутренний модуль DLL lib_ogre_module.
// Объединяет типы из src/private_ogre_components.h (SceneNodeComponent, EntityComponent)
// и include/ogre_module/ogre_components.h (OgreData, OgreSceneNode, OgreWindowClosed,
// события рендера ReadEvents/PreRender/Render/PostRender/FinalizeRender).

module;

#include <memory>

#include <SDL2/SDL_scancode.h>
#include <OgrePrerequisites.h>

// #include <ogre_module/ogre_datatypes.h>


struct SDL_Window;

export module z13.ogre.components;

export import z13.ogre.scene_node_holder;

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