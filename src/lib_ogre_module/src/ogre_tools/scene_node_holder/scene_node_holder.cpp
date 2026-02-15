#include "scene_node_holder.h"

#include <Ogre.h>

namespace z13::ogre {

static constexpr auto kSceneNodeHolderKey = "SceneNodeHolder";

SceneNodeHolder::SceneNodeHolder(Ogre::SceneNode* scene_node)
  : scene_node_(scene_node) {}


SceneNodeHolderWeakPtr SceneNodeHolder::ExtractSceneNodeHolder(const Ogre::SceneNode* scene_node) {
  auto& user_object_bindings = scene_node->getUserObjectBindings();
  const auto& scene_node_holder_any = user_object_bindings.getUserAny(kSceneNodeHolderKey);
  if (!scene_node_holder_any.has_value()) {
    return SceneNodeHolderWeakPtr();
  }

  auto* scene_node_ptr = Ogre::any_cast<SceneNodeHolderPtr>(&scene_node_holder_any);
  return scene_node_ptr ? *scene_node_ptr : SceneNodeHolderPtr();
}

SceneNodeHolderWeakPtr SceneNodeHolder::CreateSceneNodeHolder(Ogre::SceneNode* scene_node) {
  auto scene_node_ptr = SceneNodeHolder::ExtractSceneNodeHolder(scene_node);
  if (!scene_node_ptr.expired()) {
    return scene_node_ptr;
  }

  auto& user_object_bindings = scene_node->getUserObjectBindings();
  auto scene_node_holder = std::shared_ptr<SceneNodeHolder>(new SceneNodeHolder(scene_node));
  // auto scene_node_holder = std::make_shared<SceneNodeHolder>(scene_node);
  user_object_bindings.setUserAny(kSceneNodeHolderKey, scene_node_holder);
  
  return scene_node_holder;
}

Ogre::SceneNode* SceneNodeHolder::operator->() {
  return scene_node_;
}

const Ogre::SceneNode* SceneNodeHolder::operator->() const {
  return scene_node_;
}

Ogre::SceneNode* SceneNodeHolder::Get() {
  return scene_node_;
}

const Ogre::SceneNode* SceneNodeHolder::Get() const {
  return scene_node_;
}

}  // namespace z13::ogre
