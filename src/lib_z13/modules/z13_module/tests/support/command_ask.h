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
#include <string>
#include <string_view>

#include <rfl.hpp>

#include <lib_core/commands/command_registry.h>
#include <lib_core/utils/status.h>

#include "z13_test_world.h"

namespace z13::testing {

// A request waits one frame for its handlers.
inline constexpr int kCommandReplyFrames = 3;

// Asks the debug channel's commands (the "debug" tag) and ticks until the reply comes.
inline CommandResult AskCommand(
    Z13TestWorld& test_world, std::string_view command, const rfl::Object<rfl::Generic>& arguments = {}) {
  const z13::CommandFilter filter {.tags = {"debug"}};
  std::optional<CommandResult> result;
  const Status submitted = z13::SubmitCommand(test_world.World(), filter, command, rfl::Generic(arguments),
                                              [&result](CommandResult reply) { result = std::move(reply); });
  if (!submitted) {
    return std::unexpected(submitted.error());
  }
  for (int frame = 0; frame < kCommandReplyFrames && !result; ++frame) {
    test_world.Tick();
  }
  return result.value_or(std::unexpected(std::string {"no reply"}));
}

}  // namespace z13::testing
