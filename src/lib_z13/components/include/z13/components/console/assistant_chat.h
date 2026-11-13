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
#include <cstdint>
#include <deque>
#include <string>
#include <utility>
#include <vector>

namespace z13::console {

struct ChatLine {
  bool from_player {};
  std::string text;
};

// The console's talk with the assistant: what each side said, in order, and what the player said that the
// assistant has not been given yet. Runtime only, never state.
struct AssistantChat {
  using Singleton = void;

  std::deque<ChatLine> lines;
  // Grows with every line: a reader redraws only when it changed (the lines stop growing at kChatCapacity).
  std::uint64_t revision {};
  std::vector<std::string> unsent;
  bool listening {};  // someone follows the chat; set every frame by the debug server
};

// Enough to scroll back through a talk, few enough to draw every frame.
inline constexpr std::size_t kChatCapacity = 200;

inline void AddChatLine(AssistantChat& chat, ChatLine line) {
  chat.lines.push_back(std::move(line));
  ++chat.revision;
  if (chat.lines.size() > kChatCapacity) {
    chat.lines.pop_front();
  }
}

// The player's line: shown now, given to the assistant when one follows the chat.
inline void SayToAssistant(AssistantChat& chat, std::string text) {
  chat.unsent.push_back(text);
  AddChatLine(chat, {.from_player = true, .text = std::move(text)});
}

inline void AssistantSays(AssistantChat& chat, std::string text) {
  AddChatLine(chat, {.from_player = false, .text = std::move(text)});
}

}  // namespace z13::console
