// z13.ogre.tools module interface unit.
// Внутренний модуль DLL lib_ogre_module.
// Перенесён из src/ogre_tools/ogre_tools.h.

module;

#include <flecs.h>
#include <Eigen/Dense>

// #include <ogre_module/ogre_datatypes.h>

export module z13.ogre.tools;

import z13.core;
import z13.components;
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