#pragma once

#include <lib_core/components.h>

#include <z13_module/components/z13.h>

#include "ogre_tools/ogre_tools.h"
#include "ogre_tools/ogre_datatypes.h"


namespace z13::ogre {

class OgreSystem {
 public:
  static void Init(flecs::iter& iter, size_t i, Z13State& state);
  static void Render(CoreComponent& core, Z13State& state, OgreData& ogre_data);
};

}  // namespace z13::ogre
