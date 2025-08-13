#pragma once

#include <memory>

#include <OgreRoot.h>
#include "ogre_import/OgreImGuiInputListener.h"

namespace z13::ogre {

using OgreRootPtr = std::shared_ptr<Ogre::Root>;
using InputListenerPtr = std::shared_ptr<OgreBites::z13::ImGuiInputListener>;

struct OgreData {
  OgreRootPtr ogre_root;
  Ogre::RenderWindow* ogre_window = nullptr;
  InputListenerPtr input_listener;
  bool is_window_closed = false;
};

}  // namespace z13::ogre