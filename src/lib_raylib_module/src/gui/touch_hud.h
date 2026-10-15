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

#include <optional>
#include <string_view>

#include <z13/components/touch_controls.h>
#include <z13_settings/settings.h>

namespace z13::raylib::gui {

// The touch buttons and the stick under its finger (docs/android-plan.md), behind the windows;
// the tool's buttons only with a build tool in hand, the primary one labelled `tool_action`.
// The hotbar's cells are DrawHotbar's.
void DrawTouchHud(const z13::input::TouchControls& controls, const z13::TouchLayout& layout,
                  std::optional<std::string_view> tool_action);

}  // namespace z13::raylib::gui
