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

#include <lib_core/core_types.h>

namespace z13::state {

// Logs every player action (camera move/look, build, destroy, toggle build mode -- any
// action id the input config resolves) into PlayerActionLog, generically: it only ever
// reads ActionListener.action_values, so a new action never needs a code change here.
class PlayerActionRecorder {
 public:
  static void Register(flecs::world& world);
};

}  // namespace z13::state
