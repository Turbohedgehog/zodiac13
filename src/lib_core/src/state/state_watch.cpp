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


#include <lib_core/state/state_watch.h>

#include <lib_core/state/state_markers.h>

namespace z13::flecs_tools {

namespace {

TableKey KeyOf(flecs::table table) {
  return reinterpret_cast<TableKey>(table.type().array());
}

}  // namespace

TableKey TableKeyOf(flecs::entity e) {
  return KeyOf(e.table());
}

TableKey TableKeyOf(flecs::table table) {
  return KeyOf(table);
}

void StateWatch::RebuildQueries(flecs::world world) {
  queries_.clear();
  singletons_.clear();
  size_t state_components = 0;
  world.query_builder().with<StateComponent>().build().each([&](flecs::entity component) {
    ++state_components;
    const ecs_type_info_t* type_info = ecs_get_type_info(world, component.id());
    if (type_info == nullptr || type_info->size == 0) {
      return;  // a tag: entities joining a table are caught by the counts
    }
    if (component.has(flecs::Singleton)) {
      singletons_.push_back(component.id());
      return;
    }
    queries_.push_back({component.id(), world.query_builder().with(component.id()).in().detect_changes().build()});
  });
  state_components_ = state_components;
}

void StateWatch::Note(flecs::table table, bool changed) {
  const TableKey key = KeyOf(table);
  const int count = static_cast<int>(table.count());
  const auto [seen, inserted] = counts_.try_emplace(key, count);
  changed = changed || inserted || seen->second != count;
  seen->second = count;
  if (!changed) {
    return;
  }
  dirty_tables_.insert(key);
  for (const flecs::id_t id : table.type()) {
    if (!ecs_id_is_pair(id)) {
      dirty_components_.insert(id);
    }
  }
}

void StateWatch::Refresh(flecs::world world) {
  if (static_cast<size_t>(world.count<StateComponent>()) != state_components_) {
    stale_ = world.is_readonly();
    if (!stale_) {
      RebuildQueries(world);
    }
  }
  if (stale_) {
    return;
  }
  for (ComponentQuery& entry : queries_) {
    entry.query.run([&](flecs::iter& it) {
      while (it.next()) {
        Note(it.table(), it.changed());
      }
    });
  }
  for (const flecs::entity_t singleton : singletons_) {
    Note(world.entity(singleton).table(), true);  // always taken as written
  }
}

void StateWatch::MarkCaptured() {
  dirty_tables_.clear();
  dirty_components_.clear();
}

}  // namespace z13::flecs_tools
