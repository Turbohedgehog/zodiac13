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

#pragma once

#include <memory>

#include <OgrePrerequisites.h>

namespace OgreBites::z13 {

struct ImGuiInputListener;

}  // namespace OgreBites::z13

namespace Ogre {

class SceneNode;
// class SceneManager;

};  // namespace Ogre

namespace z13::ogre {

static constexpr auto kAssetsResourceGroup = "AssetsResourceGroup";

using OgreRootPtr = std::shared_ptr<Ogre::Root>;
using InputListenerPtr = std::shared_ptr<OgreBites::z13::ImGuiInputListener>;

struct SceneNodeHolder;
using SceneNodeHolderPtr = std::shared_ptr<SceneNodeHolder>;
using SceneNodeHolderWeakPtr = std::weak_ptr<SceneNodeHolder>;

}  // namespace z13::ogre
