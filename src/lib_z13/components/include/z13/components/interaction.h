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

#include <vector>

#include <flecs.h>

#include <primitives_generated.h>

namespace z13::station {

// InteractionSystem fills Interactions in this phase; a system acting on them runs in a
// phase of its own after it (docs/station-systems-plan.md, "Взаимодействие").
struct InteractionPhase {};

// `actor` interacting with the block `target` this tick, by one of the methods its primitive
// accepts.
struct Interaction {
  flecs::entity_t actor {};
  flecs::entity_t target {};
  fbs::station::InteractionMethods method {};
};

// This tick's interactions, ordered by the actors' player ids. Not state: a replayed tick
// finds them again from the actions and poses that are.
struct Interactions {
  using Singleton = void;
  std::vector<Interaction> list;
};

}  // namespace z13::station
