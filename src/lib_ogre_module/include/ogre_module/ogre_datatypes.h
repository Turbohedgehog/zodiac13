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
