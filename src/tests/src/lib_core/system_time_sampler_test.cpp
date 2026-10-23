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

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include <flecs.h>

#include <lib_core/utils/system_times.h>

namespace z13 {
namespace {

TEST(SystemTimeSamplerTest, FindsTheSystemsOnceAndSamplesTheirTotals) {
  flecs::world world;
  ecs_measure_system_time(world, true);
  world.system("first").run([](flecs::iter&) {});
  world.system("second").run([](flecs::iter&) {});
  SystemTimeSampler sampler;
  std::vector<double> totals;

  world.progress();
  sampler.Sample(world, totals);

  EXPECT_EQ(totals.size(), sampler.Paths().size());
  EXPECT_TRUE(std::ranges::contains(sampler.Paths(), std::string("::first")));
  EXPECT_TRUE(std::ranges::contains(sampler.Paths(), std::string("::second")));
  const std::vector<std::string> paths = sampler.Paths();
  world.progress();
  sampler.Sample(world, totals);
  EXPECT_EQ(sampler.Paths(), paths) << "the same systems keep their places";
}

TEST(SystemTimeSamplerTest, ASystemAddedLaterIsFoundAnew) {
  flecs::world world;
  world.system("first").run([](flecs::iter&) {});
  SystemTimeSampler sampler;
  std::vector<double> totals;
  sampler.Sample(world, totals);
  const size_t before = totals.size();

  world.system("second").run([](flecs::iter&) {});
  sampler.Sample(world, totals);

  EXPECT_EQ(totals.size(), before + 1);
  EXPECT_EQ(sampler.Paths().size(), before + 1);
  EXPECT_TRUE(std::ranges::contains(sampler.Paths(), std::string("::second")));
}

}  // namespace
}  // namespace z13
