#include "window_settings_base.h"

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

  auto world = GetWorld();

  world.set(input_config_);
  world.event<input::SystemInputEvent>()
    .id<z13::input::SaveConfigEvent>()
    .entity(world.entity().add<z13::input::SaveConfigEvent>())
    .enqueue();
  // z13::gameplay::input::InputConfigLoader::SaveConfig(input_config_);
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
