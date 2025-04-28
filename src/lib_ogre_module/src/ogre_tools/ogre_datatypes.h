#pragma once

#include <memory>

#include <OgreRoot.h>

namespace z13::ogre {

using OgreRootPtr = std::shared_ptr<Ogre::Root>;

struct OgreData {
  OgreRootPtr ogre_root;
  bool is_window_closed = false;
};

}  // namespace z13::ogre