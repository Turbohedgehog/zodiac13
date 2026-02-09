#pragma once

#include <z13_module/components/input.h>

#include "window_base.h"

namespace z13::ogre::gui {

class InputSettingsWindowBase : public WindowBase {
 public:
  InputSettingsWindowBase(flecs::world world, std::string window_name);

 protected:
  void OnBackEvent() override;
  z13::input::InputConfig& GetInputConfig();
  void SetDirty();

 private:
  void SaveIfDirty();

  z13::input::InputConfig input_config_;
  bool is_dirty_ = false;
};

}  // namespace z13::ogre::gui
