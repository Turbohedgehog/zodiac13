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
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <flecs.h>

namespace z13::flecs_tools {

// A table by its type array, which stays put while flecs keeps the table.
using TableKey = std::uintptr_t;

// Which tables of state entities were written since the last capture: one change-detecting query per state
// component. A write that doesn't mark its component changed (get_mut without modified) and a rename are missed.
class StateWatch {
 public:
  // Queries can't be built while systems run: the first call, and one after state components were added, belong
  // outside a frame; inside one a watch lacking their queries reports everything changed.
  void Refresh(flecs::world world);

  bool Unchanged(flecs::entity_t component) const { return !stale_ && !dirty_components_.contains(component); }
  bool TableChanged(TableKey table) const { return stale_ || dirty_tables_.contains(table); }

  void MarkCaptured();

 private:
  struct ComponentQuery {
    flecs::entity_t component {};
    flecs::query<> query;
  };

  void RebuildQueries(flecs::world world);
  void Note(flecs::table table, bool changed);

  std::vector<ComponentQuery> queries_;
  std::vector<flecs::entity_t> singletons_;  // a table query can't watch their value, which lives on the component
  size_t state_components_ {};
  bool stale_ {};  // queries are missing for some state components
  // Entities of a table can change without a column written: one left and one joined.
  std::unordered_map<TableKey, int> counts_;
  std::unordered_set<TableKey> dirty_tables_;
  std::unordered_set<flecs::entity_t> dirty_components_;
};

TableKey TableKeyOf(flecs::entity e);

}  // namespace z13::flecs_tools
