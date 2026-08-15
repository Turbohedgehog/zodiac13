// z13.ogre.mesh_tools module interface unit.
// Внутренний модуль DLL lib_ogre_module.
// Перенесён из src/ogre_tools/mesh_tools/mesh_tools.h.

module;

#include <flecs.h>
#include <Eigen/Dense>
#include <OgrePrerequisites.h>

// #include <ogre_module/ogre_datatypes.h>

export module z13.ogre.mesh_tools;

export namespace z13::ogre {

void CreateCubeMesh(flecs::entity& parent_entity, Ogre::Root& ogre_root, const Eigen::Matrix4f&);

}  // namespace z13::ogre