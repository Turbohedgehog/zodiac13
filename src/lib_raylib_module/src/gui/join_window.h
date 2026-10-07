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

#include <string>

#include <flecs.h>

#include "gui_windows.h"

namespace z13::raylib::gui {

class JoinWindow : public Window {
 public:
  explicit JoinWindow(flecs::world world);

  StackRequest OnBack() override;

 protected:
  void DrawBody() override;

 private:
  bool IsConnecting() const;
  void CancelIfConnecting();

  std::string host_;
  std::string port_;
  bool submitted_ {};
};

WindowPtr MakeJoinWindow(flecs::world world);

}  // namespace z13::raylib::gui
