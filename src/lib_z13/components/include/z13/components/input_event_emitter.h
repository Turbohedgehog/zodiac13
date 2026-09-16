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

#include <flecs.h>

#include <z13/components/input.h>

namespace z13::input {

// Sets EventT as source's payload and emits it as a SystemInputEventType event.
// Shared by the real input layer and by tests emulating input without SDL/raylib.
template <typename EventT>
void EmitInputEvent(flecs::world world, flecs::entity source, const EventT& event) {
  source.set<EventT>(event);
  world.event<SystemInputEventType>().id<EventT>().entity(source).emit();
}

}  // namespace z13::input
