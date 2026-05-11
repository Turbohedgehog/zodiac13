/*
 * Copyright 2026 Ivan Kulenko / Zodiac13
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://apache.org
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

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
  world.event<input::SystemInputEventType>()
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
