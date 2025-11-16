#pragma once

#include <memory>

#include <OgrePrerequisites.h>

namespace OgreBites::z13 {

struct ImGuiInputListener;

}  // namespace OgreBites::z13

namespace z13::ogre {

static constexpr auto kAssetsResourceGroup = "AssetsResourceGroup";

using OgreRootPtr = std::shared_ptr<Ogre::Root>;
using InputListenerPtr = std::shared_ptr<OgreBites::z13::ImGuiInputListener>;


}  // namespace z13::ogre