#pragma once

#include <lib_core/module_base.h>

namespace z13::ogre {

class OgreRender : public ModuleBase {
 public:
  const std::string& GetName() const override;
  void OnWorldCreated(WorldWeakPtr world) override;
};

}  // namespace z13::ogre
