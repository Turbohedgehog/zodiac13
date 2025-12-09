#include "window_settings_base.h"

#include <z13_module/input/input_config_loader.h>

namespace z13::ogre::gui {

InputSettingsWindowBase::InputSettingsWindowBase(flecs::world world, std::string window_name)
  : WindowBase(world, std::move(window_name))
  , input_config_(world.ensure<z13::input::InputConfig>()) {
}

void InputSettingsWindowBase::SaveIfDirty() {
  // LOG_INFO("==== InputSettingsWindowBase::SaveIfDirty = {}", is_dirty_);
  if (!is_dirty_) {
    return;
  }

  GetWorld().set(input_config_);
  z13::gameplay::input::InputConfigLoader::SaveConfig(input_config_);
}

void InputSettingsWindowBase::OnBackEvent() {
  SaveIfDirty();
  WindowBase::OnBackEvent();
}

void InputSettingsWindowBase::SetDirty() {
  is_dirty_ = true;
}

z13::input::InputConfig& InputSettingsWindowBase::GetInputConfig() {
  return input_config_;
}

}  // namespace z13::ogre::gui
