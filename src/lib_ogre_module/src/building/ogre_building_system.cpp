#include "ogre_building_system.h"

#include <flecs.h>
#include <Eigen/Dense>
#include <Ogre.h>

#include <z13/components/building.h>
#include <ogre_module/ogre_components.h>
#include <lib_core/log.h>

#include "../ogre_tools/mesh_tools/mesh_tools.h"
#include "../ogre_tools/ogre_tools.h"
#include "../private_ogre_components.h"

namespace z13::ogre {
struct OgreBuildingBrush {
  int a = 0;
};

void OnBuildingBrushAdded(flecs::entity e, OgreData& ogre_data, const z13::building::Brush& brush, const Eigen::Matrix4f& matrix) {
  e.add<OgreBuildingBrush>();
  CreateCubeMesh(e, *ogre_data.ogre_root, matrix);
}

void OnBuildingBrushRemoved(flecs::entity e, OgreData& ogre_data, const z13::building::Brush& brush) {
  e.remove<OgreBuildingBrush>();
  e.remove<SceneNodeComponent>();
  e.children([](flecs::entity child) {
    if (child.has<SceneNodeComponent>()) {
      child.destruct();
    }
  });
}

void OgreBuildingSystem::Register(flecs::world& world) {
  world.observer<OgreData, z13::building::Brush, Eigen::Matrix4f>("OgreBuildingSystem::OnBuildingBrushAdded")
    .event(flecs::OnAdd)
    .without<OgreBuildingBrush>()
    .yield_existing()
    .write<OgreBuildingBrush>()
    .each(OnBuildingBrushAdded);

  world.observer<OgreData, z13::building::Brush>("OgreBuildingSystem::OnBuildingBrushRemoved")
    .event(flecs::OnRemove)
    .with<OgreBuildingBrush>()
    .yield_existing()
    .write<OgreBuildingBrush>()
    .each(OnBuildingBrushRemoved);
}

}  // namespace z13::ogre
