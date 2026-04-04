#pragma once

#include <ogre_module/ogre_datatypes.h>

namespace z13::ogre {

struct SceneNodeComponent {
  Ogre::SceneNode* scene_node = nullptr;
};

struct EntityComponent {
  Ogre::Entity* entity = nullptr;
};

}  // namespace z13::ogre
