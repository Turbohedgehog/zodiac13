// z13.ogre.tools module interface unit.
// Внутренний модуль DLL lib_ogre_module.
// Перенесён из src/ogre_tools/ogre_tools.h.

module;

#include <flecs.h>
#include <Ogre.h>
#include <Eigen/Dense>

// #include <ogre_module/ogre_datatypes.h>

// #include <lib_core/log.h>

// #include <SDL2/SDL.h>
// #include <SDL2/SDL_syswm.h>

// #include <ogre_module/ogre_datatypes.h>

// #include <Ogre.h>

// #include <OgreRoot.h>
// #include <OgreOverlaySystem.h>
// #include "ogre_import/OgreImGuiOverlay.h"
// #include "ogre_import/OgreImGuiInputListener.h"
// #include "ogre_import/SDLInputMapping.h"
// #include <OgreOverlayManager.h>
// #include <OgreShaderPrerequisites.h>
// #include <OgreShaderGenerator.h>
// #include <OgreSGTechniqueResolverListener.h>

// #include <RenderSystems/GL3Plus/OgreGL3PlusPlugin.h>
// #include <Plugins/Assimp/OgreAssimpLoader.h>
// #include <Plugins/PCZSceneManager/OgrePCZPlugin.h>
// #include <Plugins/PCZSceneManager/OgrePCZSceneManager.h>
// #include <Plugins/FreeImageCodec/OgreFreeImageCodec.h>
// #include <Plugins/STBICodec/OgreSTBICodec.h>

export module z13.ogre.tools;

// import std;

export import z13.gameplay;
export import z13.ogre.components;

export namespace z13::ogre {

class OgreTools {
 public:
  static void CreateSdlOgreRoot(flecs::world world, OgreData& ogre_data);
  static void ReadSdlEvents(flecs::world world, OgreData& ogre_data);
  static void RenderSdlOgreWindow(flecs::world world, OgreData& ogre_data);
  static void DestroySdlOgreWindow(OgreData& ogre_data);
  static void EnableRelativeMouseMode(const z13::gameplay::Pause*);
  static void DisableRelativeMouseMode(const OgreData& ogre_data, const z13::gameplay::Pause&);
  static void CreateCamera(flecs::entity e, const z13::gameplay::Camera& camera, const OgreData& ogre_data);
  static void UpdateSceneNodeTransform(z13::ogre::SceneNodeComponent& scene_node_component, const Eigen::Matrix4f& transform);
  static Ogre::SceneManager* GetSceneManager(Ogre::Root& ogre_root);
};

}  // namespace z13::ogre