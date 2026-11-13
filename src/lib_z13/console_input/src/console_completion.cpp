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

#include <console_input/console_completion.h>

#include <algorithm>
#include <array>

#include <lib_core/commands/command_registry.h>
#include <z13_settings/settings.h>

#include <console_input/console_session.h>

namespace z13::console_input {

namespace {

enum class WordsOf { kCommands, kVariables };

// The commands whose first argument is a command or a setting's path.
struct ArgumentSpec {
  std::string_view command;
  WordsOf words;
};

constexpr std::array kArguments {
    ArgumentSpec {"help", WordsOf::kCommands},
    ArgumentSpec {"set", WordsOf::kVariables},
    ArgumentSpec {"settings", WordsOf::kVariables},
};

constexpr std::string_view kBlanks = " \t";

void AddCommands(flecs::world world, std::string_view typed, std::vector<std::string>& out) {
  const CommandFilter filter = ConsoleFilter();
  for (const auto& [name, entry] : world.get<Commands>().entries) {
    if (name.starts_with(typed) && FindCommand(world, filter, name) != nullptr) {
      out.push_back(name);
    }
  }
}

void AddVariables(flecs::world world, std::string_view typed, std::vector<std::string>& out) {
  for (SettingEntry& entry : ListSettings(SettingsOf(world))) {
    if (entry.path.starts_with(typed)) {
      out.push_back(std::move(entry.path));
    }
  }
}

}  // namespace

Completion CompletionOf(flecs::world world, std::string_view line) {
  Completion completion;
  const std::size_t blank = line.find_last_of(kBlanks);
  completion.word_begin = blank == std::string_view::npos ? 0 : blank + 1;
  const std::string_view typed = line.substr(completion.word_begin);
  const std::string_view before = line.substr(0, completion.word_begin);
  const std::size_t first_begin = before.find_first_not_of(kBlanks);
  if (first_begin == std::string_view::npos) {
    AddCommands(world, typed, completion.candidates);
    AddVariables(world, typed, completion.candidates);
  } else {
    const std::string_view rest = before.substr(first_begin);
    const std::string_view command = rest.substr(0, rest.find_first_of(kBlanks));
    const bool first_argument = rest.find_first_not_of(kBlanks, command.size()) == std::string_view::npos;
    const auto spec = std::ranges::find(kArguments, command, &ArgumentSpec::command);
    if (first_argument && spec != kArguments.end()) {
      if (spec->words == WordsOf::kCommands) {
        AddCommands(world, typed, completion.candidates);
      } else {
        AddVariables(world, typed, completion.candidates);
      }
    }
  }
  std::ranges::sort(completion.candidates);
  return completion;
}

std::string_view CommonPrefix(const Completion& completion) {
  if (completion.candidates.empty()) {
    return {};
  }
  std::string_view prefix = completion.candidates.front();
  for (const std::string& candidate : completion.candidates) {
    const auto [shared, ignored] = std::ranges::mismatch(prefix, candidate);
    prefix = prefix.substr(0, static_cast<std::size_t>(shared - prefix.begin()));
  }
  return prefix;
}

std::string GhostOf(std::string_view line, const Completion& completion) {
  const std::string_view typed = line.substr(completion.word_begin);
  if (typed.empty() || completion.candidates.size() != 1) {
    return {};
  }
  return completion.candidates.front().substr(typed.size());
}

std::string Replaced(std::string_view line, const Completion& completion, std::string_view text) {
  std::string result(line.substr(0, completion.word_begin));
  result += text;
  return result;
}

std::string Completed(std::string_view line, const Completion& completion, std::string_view candidate) {
  std::string result = Replaced(line, completion, candidate);
  result += ' ';
  return result;
}

}  // namespace z13::console_input
