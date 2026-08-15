/*
 * Copyright 2026 Ivan Kulenko / Zodiac13
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://apache.org
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

module;

#include <Ogre.h>

module z13.ogre.scene_node_holder;

import z13.ogre.components;

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
