#pragma once

#include <ogre_module/ogre_datatypes.h>

namespace z13::ogre {

class SceneNodeHolder {
 public:
  static SceneNodeHolderWeakPtr CreateSceneNodeHolder(Ogre::SceneNode* scene_node);

  Ogre::SceneNode* operator->();
  const Ogre::SceneNode* operator->() const;
  Ogre::SceneNode* Get();
  const Ogre::SceneNode* Get() const;

 private:
  SceneNodeHolder(Ogre::SceneNode* scene_node);
  static SceneNodeHolderWeakPtr ExtractSceneNodeHolder(const Ogre::SceneNode* scene_node);

  Ogre::SceneNode* scene_node_ = nullptr;
};

}  // namespace z13::ogre
