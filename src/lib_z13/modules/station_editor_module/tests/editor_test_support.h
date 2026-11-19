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

#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <lib_core/commands/command_registry.h>
#include <z13/components/editor/editor_view.h>

#include "../../z13_module/tests/support/z13_test_world.h"

namespace z13::station_editor {

// An editor command asked as a channel would, run for one frame; its reply, or why it was refused.
inline CommandResult RunCommand(z13::testing::Z13TestWorld& test_world, std::string_view name,
                                std::initializer_list<std::pair<std::string, rfl::Generic>> fields = {}) {
  rfl::Generic::Object arguments;
  for (const auto& [field, value] : fields) {
    arguments[field] = value;
  }
  std::optional<CommandResult> reply;
  const Status submitted = SubmitCommand(test_world.World(), {.tags = {std::string(z13::editor::kEditorTagName)}}, name,
                                         rfl::Generic(std::move(arguments)),
                                         [&reply](CommandResult result) { reply = std::move(result); });
  if (!submitted) {
    return std::unexpected(submitted.error());
  }
  test_world.Tick();
  return reply.value_or(std::unexpected("no reply"));
}

}  // namespace z13::station_editor
