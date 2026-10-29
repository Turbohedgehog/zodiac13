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

#include <lib_core/world/phase_order.h>
#include <lib_core/time/simulation_clock.h>
#include <lib_core/utils/status.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>
#include <map>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace z13 {

namespace {

using PhaseEdges = std::set<std::pair<flecs::entity_t, flecs::entity_t>>;  // {before, after}

// `from` and every phase that depends on one of them, directly or not.
std::set<flecs::entity_t> Following(std::span<const flecs::entity_t> from, const PhaseEdges& declared) {
  std::set<flecs::entity_t> following;
  std::vector<flecs::entity_t> pending(from.begin(), from.end());
  while (!pending.empty()) {
    const flecs::entity_t id = pending.back();
    pending.pop_back();
    if (!following.insert(id).second) {
      continue;
    }
    for (const auto& [before, after] : declared) {
      if (before == id) {
        pending.push_back(after);
      }
    }
  }
  return following;
}

PhaseEdges CollectEdges(flecs::world& world, const std::map<flecs::entity_t, std::string>& paths) {
  // flecs hangs these off separate hidden anchors, not off each other.
  const std::array builtin_order {
      flecs::PreFrame, flecs::OnLoad, flecs::PostLoad, flecs::PreUpdate, flecs::OnUpdate,
      flecs::OnValidate, flecs::PostUpdate, flecs::PreStore, flecs::OnStore, flecs::PostFrame};
  PhaseEdges edges;
  for (size_t i = 1; i < builtin_order.size(); ++i) {
    edges.emplace(builtin_order[i - 1], builtin_order[i]);
  }
  // A simulation phase (untagged) ends before PreStore: WorldSnapshotHistory captures the tick's
  // state in PostFrame, and a phase sorted past it by name would leave the snapshot a tick behind.
  const auto is_simulation = [&](flecs::entity_t id) {
    return !std::ranges::contains(builtin_order, id) && !world.entity(id).has<flecs_tools::PresentationPhase>();
  };
  for (const auto& [id, path] : paths) {
    if (is_simulation(id)) {
      edges.emplace(id, flecs::PreStore);
    }
  }
  PhaseEdges declared;
  for (const auto& [id, path] : paths) {
    world.entity(id).each(flecs::DependsOn, [&declared, &paths, id](flecs::entity target) {
      if (paths.contains(target.id())) {
        declared.emplace(target.id(), id);
      }
    });
  }
  edges.insert(declared.begin(), declared.end());
  std::vector<flecs::entity_t> final_phases;
  std::ranges::copy_if(std::views::keys(paths), std::back_inserter(final_phases), [&world](flecs::entity_t id) {
    return world.entity(id).has<flecs_tools::FinalStatePhase>();
  });
  const std::set<flecs::entity_t> following = Following(final_phases, declared);
  for (const auto& [id, path] : paths) {
    if (is_simulation(id) && !following.contains(id)) {
      for (const flecs::entity_t final_phase : final_phases) {
        edges.emplace(id, final_phase);
      }
    }
  }
  return edges;
}

// Kahn's algorithm, always taking the ready phase with the smallest path. Shorter than
// `paths` on a cycle.
std::vector<flecs::entity_t> SortPhases(
    const std::map<flecs::entity_t, std::string>& paths, const PhaseEdges& edges) {
  std::map<flecs::entity_t, size_t> pending_deps;
  std::map<flecs::entity_t, std::vector<flecs::entity_t>> dependents;
  for (const auto& [id, path] : paths) {
    pending_deps[id];
  }
  for (const auto& [before, after] : edges) {
    ++pending_deps[after];
    dependents[before].push_back(after);
  }

  std::set<std::pair<std::string, flecs::entity_t>> ready;
  for (const auto& [id, count] : pending_deps) {
    if (count == 0) {
      ready.emplace(paths.at(id), id);
    }
  }

  std::vector<flecs::entity_t> order;
  while (!ready.empty()) {
    const flecs::entity_t id = ready.begin()->second;
    ready.erase(ready.begin());
    order.push_back(id);
    for (const flecs::entity_t dependent : dependents[id]) {
      if (--pending_deps[dependent] == 0) {
        ready.emplace(paths.at(dependent), dependent);
      }
    }
  }
  return order;
}

}  // namespace

Status LinearizePhases(flecs::world& world) {
  // OnStart stays out: the pipeline skips anything that depends on it.
  std::map<flecs::entity_t, std::string> paths;
  world.query_builder().with(flecs::Phase).build().each([&paths](flecs::entity phase) {
    if (phase != flecs::OnStart) {
      paths.emplace(phase.id(), phase.path().c_str());
    }
  });

  const std::vector<flecs::entity_t> order = SortPhases(paths, CollectEdges(world, paths));
  if (order.size() != paths.size()) {
    return std::unexpected(std::string {"phase DependsOn graph has a cycle"});
  }

  flecs::entity previous_anchor;
  for (const flecs::entity_t id : order) {
    const flecs::entity phase = world.entity(id);
    std::vector<flecs::entity_t> targets;
    phase.each(flecs::DependsOn, [&targets](flecs::entity target) { targets.push_back(target.id()); });
    for (const flecs::entity_t target : targets) {
      phase.remove(flecs::DependsOn, target);
    }

    const flecs::entity anchor = world.entity();
    if (previous_anchor) {
      anchor.depends_on(previous_anchor);
    }
    phase.depends_on(anchor);
    previous_anchor = anchor;
  }
  return {};
}

}  // namespace z13
