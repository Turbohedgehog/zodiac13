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
#include <optional>
#include <string>
#include <string_view>

#include <flecs.h>

#include <console_input/console_completion.h>
#include <console_input/line_completion.h>

namespace z13::testing {

// An ImGui text field as the console's completion sees it: typing replaces what is selected, Backspace deletes the
// selection or the last character, and each Frame() is one look of the field's callback.
class FieldSim {
 public:
  explicit FieldSim(flecs::world world) : world_(world) {}

  void Type(std::string_view characters) {
    for (const char character : characters) {
      EraseSelection();
      text_ += character;
    }
  }

  void Backspace() {
    if (!EraseSelection() && !text_.empty()) {
      text_.pop_back();
    }
  }

  // The program replaces the text (history, Tab).
  void Replace(std::string_view text) {
    text_ = text;
    selection_begin_.reset();
    console_input::ResetLineCompletion(state_, text_);
  }

  void Frame() {
    const auto step = console_input::StepLineCompletion(
        state_, text_, at_end_ && !selection_begin_, [this](std::string_view line) {
          return console_input::CompletionOf(world_, line);
        });
    if (!step.changed || !step.completes) {
      return;
    }
    list_ = step.list;
    if (!step.rest.empty()) {
      selection_begin_ = text_.size();
      text_ += step.rest;
    }
  }

  // The cursor is somewhere but the end of the text.
  void MoveCursorAwayFromTheEnd() { at_end_ = false; }

  const std::string& Text() const { return text_; }
  std::string Selected() const { return selection_begin_ ? text_.substr(*selection_begin_) : std::string(); }
  bool ListOpen() const { return list_.has_value(); }

 private:
  bool EraseSelection() {
    if (!selection_begin_) {
      return false;
    }
    text_.erase(*selection_begin_);
    selection_begin_.reset();
    return true;
  }

  flecs::world world_;
  console_input::LineCompletionState state_;
  std::string text_;
  std::optional<std::size_t> selection_begin_;
  std::optional<console_input::Completion> list_;
  bool at_end_ {true};
};

}  // namespace z13::testing
