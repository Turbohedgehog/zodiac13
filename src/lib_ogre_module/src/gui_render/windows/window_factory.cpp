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

#include "window_factory.h"

#include "gameplay_main_menu.h"
// #include "options_input_keyboard_bindings.h"
#include "options_input_keyboard_bindings_2.h"
#include "options_input_settings.h"

namespace z13::ogre::gui {

WindowPtr WindowFactory::CreateGameplayMainMenu(flecs::world world) {
  return std::make_shared<GameplayMainMenuWindow>(world);
}

WindowPtr WindowFactory::CreateInputSettingsMenu(flecs::world world) {
  return std::make_shared<InputSettingsWindow>(world);
}

WindowPtr WindowFactory::CreateKeyboardBindingsMenu(flecs::world world) {
  return std::make_shared<KeyBindingWindow2>(world);
}

}  // namespace z13::ogre::gui
