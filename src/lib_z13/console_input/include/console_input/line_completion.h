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

#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include <console_input/console_completion.h>

namespace z13::console_input {

// The completions for a line, as CompletionOf makes them for a world.
using CompletionSource = std::function<Completion(std::string_view line)>;

// What the field has been through, to tell typing from deleting and from its own selected rest. No ImGui here, so a
// test can play a field's edits.
struct LineCompletionState {
  // The text as typed, without the selected rest after it.
  std::string typed;
  // The text as the last step left it.
  std::string seen;
};

// What the field shows after a step.
struct LineCompletionStep {
  // False: the text is as the last step left it, nothing to do.
  bool changed {};
  // False: the cursor is not at the end or text is selected, so the list and the rest stay as they were.
  bool completes {};
  // The rest to put after the text, selected; empty for none.
  std::string rest;
  // The list to open, when several candidates start with the last word; none closes it.
  std::optional<Completion> list;
};

// One look at the field's text, every frame. A typed character gives the rest of the only match, or a list of
// several; a deletion gives a list but never a rest, or Backspace could not clear it. `at_end`: the cursor is at the
// end and nothing is selected.
LineCompletionStep StepLineCompletion(LineCompletionState& state, std::string_view text, bool at_end,
                                      const CompletionSource& source);

// The field's text was replaced by the program (history, Tab): nothing in it is typed or deleted.
void ResetLineCompletion(LineCompletionState& state, std::string_view text);

}  // namespace z13::console_input
