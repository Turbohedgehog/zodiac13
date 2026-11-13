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

#include <console_input/line_completion.h>

namespace z13::console_input {

LineCompletionStep StepLineCompletion(LineCompletionState& state, std::string_view text, bool at_end,
                                      const CompletionSource& source) {
  LineCompletionStep step;
  if (text == state.seen) {
    return step;
  }
  step.changed = true;
  const bool grew = text.size() > state.typed.size() && text.starts_with(state.typed);
  state.typed = text;
  state.seen = text;
  if (!at_end) {
    return step;
  }
  step.completes = true;
  Completion completion = source(text);
  const bool word_typed = completion.word_begin < text.size();
  if (word_typed && completion.candidates.size() > 1) {
    step.list = std::move(completion);
  } else if (grew) {
    step.rest = GhostOf(text, completion);
    state.seen += step.rest;
  }
  return step;
}

void ResetLineCompletion(LineCompletionState& state, std::string_view text) {
  state.typed = text;
  state.seen = text;
}

}  // namespace z13::console_input
