#pragma once

#include <lib_core/system_base.h>

namespace z13::ogre {

class OgreSystem : public SystemBase {
 public:
  void OnAddedToWorld(WorldPtr world) override;
  void Update(double delta_time) override;
};

}  // namespace z13::ogre
