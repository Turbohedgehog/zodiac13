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
#include <optional>
#include <string>
#include <vector>

#include <Eigen/Geometry>

#include <console_input/console_completion.h>
#include <console_input/line_completion.h>

#include "report_view.h"

namespace z13::raylib::gui {

enum class ConsoleTab { kLog, kAssistant };

// The console's own state (docs/console-plan.md): local to this client, never state.
struct ConsoleState {
  using Singleton = void;

  bool open {};
  // 0 closed, 1 fully down; follows `open` over a moment.
  float slide {};
  ConsoleTab tab = ConsoleTab::kLog;
  std::string command_input;
  std::string chat_input;
  std::vector<std::string> history;
  // The list over the field: the choices for the line `suggestions_line`, and the one the arrows are on.
  std::optional<console_input::Completion> suggestions;
  std::string suggestions_line;
  std::size_t suggestion {};
  bool suggestion_moved {};
  // A finger is dragging the list: it scrolls, and lifting it chooses nothing.
  bool suggestions_dragged {};
  // The history entry the arrows show; none while a new line is being typed.
  std::optional<std::size_t> history_position;
  // The log and the talk as drawn: long press selects lines to copy (ReportView). A view with a selection is not
  // redrawn until the selection goes, so it does not slip away under the finger.
  ReportView log_view {"##console_log"};
  ReportView chat_view {"##console_chat"};
  std::uint64_t log_revision {};
  std::uint64_t chat_revision {};
  bool focus_input {};
  // The field got the focus back: the cursor goes to the end of its text, because ImGui selects all of it.
  bool cursor_to_end {};
  console_input::LineCompletionState line_completion;
  // The console's key was pressed this frame: the character it types (the ё of a Russian layout) is not for any field.
  bool ignore_text {};
  // The soft keyboard is wanted (touch screens only).
  bool keyboard {};
  // The button that opens the console, as last drawn, in fractions of the screen: a touch on it is the
  // console's, not the game's.
  std::optional<Eigen::AlignedBox2f> button_area;
};

}  // namespace z13::raylib::gui
