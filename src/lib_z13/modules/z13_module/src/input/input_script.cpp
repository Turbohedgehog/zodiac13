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

#include "input_script.h"

#include <charconv>
#include <format>
#include <iterator>
#include <ranges>
#include <sstream>

namespace z13::gameplay::input {

namespace {

constexpr char kCommentStart = '#';
constexpr std::string_view kDown = "down";
constexpr std::string_view kUp = "up";
constexpr std::string_view kTurn = "turn";
constexpr std::string_view kQuit = "quit";

std::optional<z13::fbs::input::Keycode> KeycodeNamed(std::string_view name) {
  for (const z13::fbs::input::Keycode key : z13::fbs::input::EnumValuesKeycode()) {
    if (name == z13::fbs::input::EnumNameKeycode(key)) {
      return key;
    }
  }
  return std::nullopt;
}

template <typename T>
std::optional<T> Number(std::string_view text) {
  T value {};
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc {} || end != text.data() + text.size()) {
    return std::nullopt;
  }
  return value;
}

std::expected<ScriptStep, std::string> ParseLine(const std::vector<std::string>& words) {
  const auto tick = Number<uint64_t>(words[0]);
  if (!tick) {
    return std::unexpected(std::format("'{}' is not a tick", words[0]));
  }
  if (words.size() == 2 && words[1] == kQuit) {
    return ScriptStep {.tick = *tick, .action = ScriptAction::kQuit};
  }
  if (words.size() == 4 && words[1] == kTurn) {
    const auto yaw = Number<float>(words[2]);
    const auto pitch = Number<float>(words[3]);
    if (!yaw || !pitch) {
      return std::unexpected("turn needs YAW PITCH in degrees");
    }
    return ScriptStep {.tick = *tick, .action = ScriptAction::kTurn, .turn_degrees = {*yaw, *pitch}};
  }
  if (words.size() == 3 && (words[2] == kDown || words[2] == kUp)) {
    const auto key = KeycodeNamed(words[1]);
    if (!key) {
      return std::unexpected(std::format("no key named '{}'", words[1]));
    }
    return ScriptStep {
        .tick = *tick, .action = words[2] == kDown ? ScriptAction::kKeyDown : ScriptAction::kKeyUp, .key = key};
  }
  return std::unexpected("expected `KEY down`, `KEY up`, `turn YAW PITCH` or `quit`");
}

}  // namespace

std::expected<std::vector<ScriptStep>, std::string> ParseInputScript(std::string_view text) {
  std::vector<ScriptStep> steps;
  size_t line_number = 0;
  for (const auto line_range : std::views::split(text, '\n')) {
    ++line_number;
    std::string line(line_range.begin(), line_range.end());
    line = line.substr(0, line.find(kCommentStart));
    std::istringstream stream(line);
    std::vector<std::string> words {std::istream_iterator<std::string>(stream), std::istream_iterator<std::string>()};
    if (words.empty()) {
      continue;
    }
    auto step = ParseLine(words);
    if (!step) {
      return std::unexpected(std::format("line {}: {}", line_number, step.error()));
    }
    if (!steps.empty() && step->tick < steps.back().tick) {
      return std::unexpected(std::format("line {}: tick {} comes before {}", line_number, step->tick, steps.back().tick));
    }
    steps.push_back(*step);
  }
  return steps;
}

}  // namespace z13::gameplay::input
