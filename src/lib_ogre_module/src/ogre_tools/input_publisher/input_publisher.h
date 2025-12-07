#pragma once

#include <flecs.h>
#include "OgreInput.h"

namespace z13::ogre {

class InputPublisher {
 public:
  static bool PublishInput(flecs::world world, const OgreBites::Event& ogre_event);
};

}  // namespace z13::ogre
