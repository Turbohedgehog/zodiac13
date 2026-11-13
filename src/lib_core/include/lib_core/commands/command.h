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

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace z13 {

// A string literal as a template argument.
template <std::size_t N>
struct FixedText {
  constexpr FixedText(const char (&text)[N]) { std::copy_n(text, N, characters.begin()); }  // NOLINT: literal

  constexpr std::string_view View() const { return {characters.data(), N - 1}; }

  std::array<char, N> characters {};
};

// What a command is for, not where it is called from: each channel (console, debug server, ...) says which
// tags it shows, so the core knows no channel. The tags are "debug" (safe to look and probe), "cheat" and "admin".
template <FixedText... kNames>
struct Tags {
  static constexpr std::array<std::string_view, sizeof...(kNames)> kValues {kNames.View()...};
};

// The tag settings.fbs lists as the debug channel's default (a test keeps the two equal).
inline constexpr FixedText kDebugTag("debug");

// Edits the station: lays, builds or removes (the commands of building_module and conduit_module).
inline constexpr FixedText kBuildTag("build");

template <FixedText kText>
struct CommandName {
  static constexpr std::string_view kValue = kText.View();
};

// A command is a request: a struct whose fields are its arguments (an aggregate rfl reflects: parsing and the
// JSON schema come from it) with a nested Command alias, registered through RegisterComponent; systems made with
// CommandSystem handle it. Without a Name it is called after its type ("TeleportCommand" is "teleport").
//
//   struct Teleport {
//     using Command = z13::Command<std::string, "Puts the player at a point, metres.", Tags<"debug">>;
//     float x {};
//     float y {};
//   };
template <class ResultType, FixedText kDescription, class TagList = Tags<>, class Name = void>
struct Command {
  using Result = ResultType;
  using TagsOf = TagList;
  using NameSource = Name;

  static constexpr std::string_view kText = kDescription.View();
};

template <class T>
concept CommandComponentType = requires { typename T::Command; };

// The answer a command's handlers fill in, on the request's entity.
template <class R>
struct CommandReply {
  void Succeed(R new_value) {
    done = true;
    failed = false;
    value = std::move(new_value);
    error.clear();
  }

  void Fail(std::string message) {
    done = true;
    failed = true;
    error = std::move(message);
  }

  bool done {};
  bool failed {};
  R value {};
  std::string error;
};

template <CommandComponentType T>
using ReplyOf = CommandReply<typename T::Command::Result>;

// A request in flight; its entity goes when CommandCollect has handed the reply back.
struct CommandRequest {
  std::uint64_t ticket {};
  std::uint32_t age {};  // frames the collector has seen it
};

// Where the handlers run, and the collection of their replies right after.
struct CommandPhase {};
struct CommandCollectPhase {};

}  // namespace z13
