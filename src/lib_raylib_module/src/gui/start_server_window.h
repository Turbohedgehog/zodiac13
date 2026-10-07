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

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <flecs.h>

#include "gui_windows.h"

namespace z13::raylib::gui {

// Success removes Pause, which closes the menu; only a failure is left to show here.
class StartServerWindow : public Window {
 public:
  explicit StartServerWindow(flecs::world world);

 protected:
  void DrawBody() override;

 private:
  void DrawSceneCombo();

  std::string port_;
  bool station_ {};
  // The blueprints under assets/station/blueprints/; `scene_` indexes them, none is empty.
  std::vector<std::string> station_scenes_;
  std::optional<size_t> scene_;
  bool submitted_ {};
};

WindowPtr MakeStartServerWindow(flecs::world world);

}  // namespace z13::raylib::gui
