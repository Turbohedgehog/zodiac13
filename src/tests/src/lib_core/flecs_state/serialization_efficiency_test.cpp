// Opt-in benchmark: is a binary format worth adding next to JSON for world state?
// Not run by default (DISABLED_ prefix). To run:
//   z13_test_runner --gtest_also_run_disabled_tests --gtest_filter=*BinaryVsJson*

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <flecs.h>
#include <rfl/json.hpp>

#include <flatbuffers/flatbuffers.h>

#include <lib_core/component_codec.h>
#include <lib_core/world_json_store.h>
#include <lib_core/world_serializer.h>

#include "test_components.h"
#include "world_fixture.h"

namespace z13::tests {
namespace {

namespace ft = z13::flecs_tools;

using Clock = std::chrono::steady_clock;

const ft::EntityFilter kTestFilter = [](flecs::entity e) { return e.has<TestEntity>(); };

// The fixture world's snapshot, tiled to `entity_count` entities with unique names.
ft::WorldSnapshot MakeBenchmarkSnapshot(const flecs::world& world, std::size_t entity_count) {
  const ft::WorldSnapshot base = ft::CaptureWorld(world, kTestFilter).value();

  ft::WorldSnapshot inflated;
  inflated.entities.reserve(entity_count);
  for (std::size_t i = 0; inflated.entities.size() < entity_count; ++i) {
    ft::EntitySnapshot duplicate = base.entities[i % base.entities.size()];
    duplicate.name += "_" + std::to_string(i);
    inflated.entities.push_back(std::move(duplicate));
  }
  return inflated;
}

// A real flecs world with `entity_count` named entities, so SaveWorldState
// (capture + canonical sort + write) can be measured end to end.
flecs::world MakeBenchmarkWorld(std::size_t entity_count) {
  flecs::world world;
  RegisterTestComponents(world);
  std::vector<flecs::entity> created;
  created.reserve(entity_count);
  for (std::size_t i = 0; i < entity_count; ++i) {
    flecs::entity e = world.entity(("e_" + std::to_string(i)).c_str()).add<TestEntity>();
    e.set<Position>({static_cast<float>(i), 0.f, static_cast<float>(i % 7)});
    if (i % 2 == 0) e.set<Health>({100, 100});
    if (i % 3 == 0) e.set<Label>({"x"});
    if (i % 5 == 0) e.add<PlayerTag>();
    if (i % 4 == 0 && !created.empty()) e.add<Likes>(created[i % created.size()]);
    created.push_back(e);
  }
  return world;
}

// Minimum nanoseconds for `ops` calls to op(), over `samples` samples (one
// warm-up sample is discarded). `sink` keeps the results observable.
template <class Op>
std::int64_t BestNs(const Op& op, std::size_t& sink, int ops, int samples) {
  std::int64_t best = std::numeric_limits<std::int64_t>::max();
  for (int sample = 0; sample <= samples; ++sample) {
    const auto start = Clock::now();
    for (int i = 0; i < ops; ++i) {
      sink += op();
    }
    const auto ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
    if (sample > 0) {
      best = std::min(best, ns);
    }
  }
  return best;
}

std::vector<uint8_t> WriteBinary(const ft::WorldSnapshot& snapshot) {
  const z13::fbs::state::WorldSnapshotT flat = ft::ToFlatbuffer(snapshot);
  flatbuffers::FlatBufferBuilder builder;
  builder.Finish(z13::fbs::state::WorldSnapshot::Pack(builder, &flat));
  return {builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize()};
}

ft::WorldSnapshot ReadBinary(const std::vector<uint8_t>& bytes) {
  z13::fbs::state::WorldSnapshotT flat;
  z13::fbs::state::GetWorldSnapshot(bytes.data())->UnPackTo(&flat);
  return ft::FromFlatbuffer(flat);
}

// Stage 1's format: values as flecs JSON text.
std::vector<uint8_t> WriteWithJsonValues(const flecs::world& world, const ft::WorldSnapshot& snapshot) {
  z13::fbs::state::WorldSnapshotT flat = ft::ToFlatbuffer(snapshot);
  for (const auto& entity : flat.entities) {
    for (const auto& component : entity->components) {
      const std::string json =
          ft::ValueToJson(world, world.lookup(component->type.c_str()), component->value).value();
      component->value.assign(json.begin(), json.end());
    }
  }
  flatbuffers::FlatBufferBuilder builder;
  builder.Finish(z13::fbs::state::WorldSnapshot::Pack(builder, &flat));
  return {builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize()};
}

TEST(SerializationEfficiency, BinaryValuesShrinkTheSnapshot) {
  flecs::world world;
  PopulateFixtureWorld(world);
  const ft::WorldSnapshot snapshot = MakeBenchmarkSnapshot(world, /*entity_count=*/30);

  const std::size_t binary = WriteBinary(snapshot).size();
  const std::size_t with_json = WriteWithJsonValues(world, snapshot).size();

  std::cout << "[ size  ] binary values " << binary << " bytes, JSON values " << with_json << " bytes ("
            << static_cast<double>(binary) / static_cast<double>(with_json) << "x)\n";
  EXPECT_LT(binary, with_json);
}

// Binary snapshot against the JSON save format; prints the numbers only.
void RunBenchmark(std::size_t entity_count, int ops, int samples) {
  flecs::world fixture;
  PopulateFixtureWorld(fixture);
  const ft::WorldSnapshot snapshot = MakeBenchmarkSnapshot(fixture, entity_count);

  const std::string json_bytes = ft::WorldJsonStore::ToJson(fixture, snapshot).value();
  const std::vector<uint8_t> binary_bytes = WriteBinary(snapshot);

  // Both formats must round-trip the same data.
  ASSERT_EQ(rfl::json::write(ReadBinary(binary_bytes)),
            rfl::json::write(ft::WorldJsonStore::FromJson(fixture, json_bytes).value()));

  std::size_t sink = 0;

  const std::int64_t json_write = BestNs(
      [&] { return ft::WorldJsonStore::ToJson(fixture, snapshot).value().size(); }, sink, ops, samples);
  const std::int64_t json_read = BestNs(
      [&] { return ft::WorldJsonStore::FromJson(fixture, json_bytes).value().entities.size(); },
      sink, ops, samples);
  const std::int64_t binary_write = BestNs(
      [&] { return WriteBinary(snapshot).size(); }, sink, ops, samples);
  const std::int64_t binary_read = BestNs(
      [&] { return ReadBinary(binary_bytes).entities.size(); },
      sink, ops, samples);
  EXPECT_GT(sink, 0u);

  const std::int64_t json_total = json_write + json_read;
  const std::int64_t binary_total = binary_write + binary_read;
  const double round_trip_ratio = static_cast<double>(binary_total) / static_cast<double>(json_total);

  std::cout << "[ bench ] " << entity_count << " entities, best of " << samples << " x " << ops
            << " ops (us) | payload bytes\n"
            << "[ bench ]   json   write " << json_write / 1000 << ", read " << json_read / 1000
            << ", total " << json_total / 1000 << " | " << json_bytes.size() << "\n"
            << "[ bench ]   binary write " << binary_write / 1000 << ", read " << binary_read / 1000
            << ", total " << binary_total / 1000 << " | " << binary_bytes.size() << "\n"
            << "[ bench ]   binary round-trip is " << round_trip_ratio << "x of json ("
            << (binary_total < json_total ? "binary faster" : "json faster") << "), payload "
            << static_cast<double>(binary_bytes.size()) / static_cast<double>(json_bytes.size())
            << "x\n";

  // Full capture+sort+write pipeline on a real world (this path holds the sorts).
  const flecs::world world = MakeBenchmarkWorld(entity_count);
  const std::int64_t save = BestNs(
      [&] { return ft::SaveWorldState(world, kTestFilter).value().size(); }, sink, ops, samples);
  std::cout << "[ bench ]   SaveWorldState (capture+sort+write) " << save / 1000 << " us\n";
}

TEST(SerializationEfficiency, DISABLED_BinaryVsJson) {
  RunBenchmark(/*entity_count=*/30, /*ops=*/60, /*samples=*/5);
}

TEST(SerializationEfficiency, DISABLED_BinaryVsJsonLargeWorld) {
  RunBenchmark(/*entity_count=*/10000, /*ops=*/2, /*samples=*/2);
}

}  // namespace
}  // namespace z13::tests
