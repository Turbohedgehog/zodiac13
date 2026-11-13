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


#include <console_input/console_session.h>

#include <string>

#include <lib_core/utils/flecs_utils.h>
#include <lib_core/utils/log.h>

#include <console_input/console_input.h>
#include <console_input/console_variables.h>

namespace z13::console_input {

namespace {

constexpr std::string_view kPromptPrefix = "> ";

}  // namespace

CommandFilter ConsoleFilter() {
  return {.tags = {std::string(kDebugTag.View()), std::string(kConsoleTag.View())}};
}

void RunConsoleLine(flecs::world world, std::string_view line) {
  if (line.find_first_not_of(" \t") == std::string_view::npos) {
    return;
  }
  log_info("{}{}", kPromptPrefix, line);
  const auto reply = [](CommandResult result) {
    if (result) {
      if (const std::string text = ReplyText(*result); !text.empty()) {
        log_info("{}", text);
      }
    } else {
      log_error("{}", result.error());
    }
  };
  if (const auto words = SplitCommandLine(line);
      words && !words->empty() && FindCommand(world, ConsoleFilter(), words->front()) == nullptr) {
    if (const auto variable = RunVariable(world, *words)) {
      reply(*variable);
      return;
    }
  }
  Status submitted;
  {
    const ImmediateScope immediate(world);
    submitted = SubmitCommandLine(world, ConsoleFilter(), line, reply);
  }
  if (!submitted) {
    reply(std::unexpected(submitted.error()));
  }
}

}  // namespace z13::console_input
