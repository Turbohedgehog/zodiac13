#pragma once

#include <Eigen/Dense>

#include <lib_core/core_types.h>
#include <ogre_module/ogre_datatypes.h>

namespace z13::ogre {

void CreateCubeMesh(flecs::entity& parent_entity, Ogre::Root& ogre_root, const Eigen::Matrix4f&);

}   // namespace z13::ogre
