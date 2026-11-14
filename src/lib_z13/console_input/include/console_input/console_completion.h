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
#include <string>
#include <string_view>
#include <vector>

#include <flecs.h>

namespace z13::console_input {

// What Tab can put in place of the word the line ends with.
struct Completion {
  // Where that word starts in the line.
  std::size_t word_begin {};
  // Sorted; each starts with the word.
  std::vector<std::string> candidates;
};

// The commands the console shows and the settings' paths (variables) that start with the line's last word; after
// `set` or `settings` only the paths, after `help` only the commands.
Completion CompletionOf(flecs::world world, std::string_view line);

// What to show, selected, after the line as the player types: the rest of the only candidate. Empty when there are
// several (they get a list) or none, or when the last word is empty.
std::string GhostOf(std::string_view line, const Completion& completion);

// The longest start all candidates share.
std::string_view CommonPrefix(const Completion& completion);

// The line with its last word replaced by `text`.
std::string Replaced(std::string_view line, const Completion& completion, std::string_view text);

// The line with its last word replaced by the candidate, and a space to go on typing the arguments.
std::string Completed(std::string_view line, const Completion& completion, std::string_view candidate);

}  // namespace z13::console_input
