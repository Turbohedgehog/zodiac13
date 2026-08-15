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

module;

// #pragma warning(push)
// #pragma warning(disable: 4291) // Игнорируем отсутствие парного operator delete
// #include <flecs.h> // или ваши модульные импорты, если они тянут flecs
// #pragma warning(pop)

export module z13.ogre.gui.gameplay_main_menu;

import z13.ogre.gui.window_base;

export namespace z13::ogre::gui {

class GameplayMainMenuWindow : public WindowBase {
 public:
  GameplayMainMenuWindow(flecs::world world);

  void OnBackEvent() override;

 protected:
  void DrawImpl() override;
};

}  // namespace z13::ogre::gui
