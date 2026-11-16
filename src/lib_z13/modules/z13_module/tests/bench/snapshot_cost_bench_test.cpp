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

// A bench, not a test: what a periodic snapshot of the hub costs and which entity types it is spent on.
//   z13_test_runner --gtest_also_run_disabled_tests --gtest_filter='SnapshotCostBench.*'

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <format>
#include <iostream>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <lib_core/state/rollback.h>
#include <lib_core/state/snapshot_hash.h>
#include <lib_core/state/state_markers.h>
#include <lib_core/state/world_serializer.h>
#include <lib_core/state/world_snapshot_history.h>
#include <lib_core/time/simulation_clock.h>
#include <net_module/state_digest.h>
#include <z13_tests/shipped_station.h>

#include "../support/z13_test_world.h"

namespace z13::testing {
namespace {

namespace ft = z13::flecs_tools;

constexpr int kWarmUpTicks = 300;
// A game's snapshot comes one second of simulation after the previous one.
constexpr int kTicksBetweenSnapshots = 60;
constexpr int kSnapshots = 5;
constexpr size_t kTopTypes = 20;

double Ms(std::chrono::steady_clock::duration duration) {
  return std::chrono::duration<double, std::milli>(duration).count();
}

// The state components of an entity, which is what decides how it is encoded.
std::string TypeKey(const ft::EntitySnapshot& entity) {
  std::string key;
  for (const ft::ComponentValue& component : entity.components) {
    const size_t name_start = component.type.rfind("::");
    key += (name_start == std::string::npos ? component.type : component.type.substr(name_start + 2)) + " ";
  }
  for (const std::string& tag : entity.tags) {
    const size_t name_start = tag.rfind("::");
    key += (name_start == std::string::npos ? tag : tag.substr(name_start + 2)) + "+ ";
  }
  return key;
}

struct TypeCost {
  size_t entities {};
  size_t bytes {};
  double ms {};
};

// Each state entity encoded on its own, as a snapshot does when nothing of it is cached.
std::map<std::string, TypeCost> EncodeCostByType(flecs::world world) {
  std::map<std::string, TypeCost> costs;
  world.query_builder().with<ft::StateEntity>().with<flecs::Identifier>(flecs::Name).build().each(
      [&](flecs::entity e) {
        if (!ft::StateEntityFilter(e)) {
          return;
        }
        const auto start = std::chrono::steady_clock::now();
        const auto captured = ft::CaptureEntityState(world, e);
        const double ms = Ms(std::chrono::steady_clock::now() - start);
        if (!captured) {
          return;
        }
        for (const ft::SharedEntity& entity : captured->entities) {
          TypeCost& cost = costs[TypeKey(*entity)];
          ++cost.entities;
          cost.ms += ms;
          for (const ft::ComponentValue& component : entity->components) {
            cost.bytes += component.value.size();
          }
        }
      });
  return costs;
}

void PrintCosts(const std::map<std::string, TypeCost>& costs) {
  std::vector<std::pair<std::string, TypeCost>> sorted(costs.begin(), costs.end());
  std::ranges::sort(sorted, std::greater<> {}, [](const auto& entry) { return entry.second.ms; });
  const double total_ms = std::ranges::fold_left(sorted, 0.0, [](double sum, const auto& e) { return sum + e.second.ms; });
  size_t total_entities = 0;
  for (const auto& entry : sorted) {
    total_entities += entry.second.entities;
  }
  std::cout << std::format("[ bench ] encoded one by one: {} entities, {:.1f} ms, {} types\n", total_entities, total_ms,
                           sorted.size());
  for (const auto& [key, cost] : std::span(sorted).first(std::min(sorted.size(), kTopTypes))) {
    std::cout << std::format("[ bench ] {:8.2f} ms {:5.1f}% {:6} entities {:9} bytes  {}\n", cost.ms,
                             100.0 * cost.ms / total_ms, cost.entities, cost.bytes, key);
  }
}

}  // namespace

TEST(SnapshotCostBench, DISABLED_HubSnapshot) {
  Z13TestWorld world({std::string(kStationSceneArg), std::string(kHubScene)});
  flecs::world w = world.World();
  w.set<ft::SnapshotCaptureRate>({.per_interval = 0});
  world.Tick();
  for (int tick = 0; tick < kWarmUpTicks; ++tick) {
    world.Tick();
  }

  PrintCosts(EncodeCostByType(w));

  const auto first_start = std::chrono::steady_clock::now();
  auto snapshot = ft::CaptureState(w);
  ASSERT_TRUE(snapshot.has_value()) << snapshot.error();
  std::cout << std::format("[ bench ] first CaptureState {:.1f} ms\n", Ms(std::chrono::steady_clock::now() - first_start));

  for (int round = 0; round < kSnapshots; ++round) {
    for (int tick = 0; tick < kTicksBetweenSnapshots; ++tick) {
      world.Tick();
    }
    const auto capture_start = std::chrono::steady_clock::now();
    auto captured = ft::CaptureState(w);
    const double capture_ms = Ms(std::chrono::steady_clock::now() - capture_start);
    ASSERT_TRUE(captured.has_value()) << captured.error();

    const auto hash_start = std::chrono::steady_clock::now();
    const uint64_t hash = ft::HashSnapshot(*captured);
    const double hash_ms = Ms(std::chrono::steady_clock::now() - hash_start);

    const ft::TimestampedSnapshot stamped {.tick = w.get<z13::flecs_tools::SimulationClock>().tick,
                                           .snapshot = std::move(*captured)};
    const auto digest_start = std::chrono::steady_clock::now();
    const auto digest = z13::net::ComputeStateDigest(w, stamped);
    const double digest_ms = Ms(std::chrono::steady_clock::now() - digest_start);

    std::cout << std::format(
        "[ bench ] snapshot {}: CaptureState {:.1f} ms, HashSnapshot {:.1f} ms, ComputeStateDigest {:.1f} ms "
        "({} entities, {} groups, hash {:x})\n",
        round, capture_ms, hash_ms, digest_ms, stamped.snapshot.entities.size(), stamped.snapshot.groups.size(), hash);
    EXPECT_GT(digest.entity_count, 0U);
  }
}

}  // namespace z13::testing
