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

// The frame report of the last seconds before the pause, to read, copy or save, and to start
// the measuring over.
class ProfileWindow : public Window {
 public:
  explicit ProfileWindow(flecs::world world);

 protected:
  void DrawBody() override;

 private:
  std::string report_;
  std::string status_;
};

WindowPtr MakeProfileWindow(flecs::world world);

// Copies the report to the clipboard and saves it; returns what it did, for a log or a window.
std::string CaptureProfileReport(const std::string& report);

}  // namespace z13::raylib::gui
