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
#include <cstdint>
#include <format>
#include <numeric>
#include <string>
#include <string_view>
#include <vector>

#include <z13/components/input.h>
#include <z13_module/input/action_negotiation.h>
#include <z13_module/input/input_config_loader.h>

#include "../support/z13_test_world.h"

namespace z13::gameplay::input {
namespace {

using z13::input::ActionInfo;
using z13::input::ActionMap;
using z13::input::InputConfig;
using z13::testing::Z13TestWorld;

constexpr std::string_view kExtraEnum = "test.Extra";
constexpr std::string_view kExtraValue = "BOOST";
constexpr ActionInfo::EnumValueType kExtraEnumValue = 700;
constexpr uint32_t kShift = 10;

ActionMap& Map(Z13TestWorld& test_world) { return test_world.World().get_mut<ActionMap>(); }

ActionDescriptor Extra(std::string value_name = std::string(kExtraValue), ActionInfo::EnumValueType enum_value = kExtraEnumValue) {
  return {.enum_name = std::string(kExtraEnum), .value_name = std::move(value_name), .enum_value = enum_value};
}

ActionInfo::IdType MaxId(const ActionMap& map) {
  return map.action_map.get<ActionMap::IdTag>().rbegin()->id;
}

TEST(ActionNegotiationTest, KnownActionsKeepTheirIds) {
  Z13TestWorld test_world;
  const auto described = DescribeActions(Map(test_world));
  const size_t count_before = Map(test_world).action_map.size();

  const auto ids = RegisterRemoteActions(Map(test_world), described);

  ASSERT_TRUE(ids.has_value()) << ids.error();
  ASSERT_EQ(ids->size(), described.size());
  size_t index = 0;
  for (const ActionInfo& info : Map(test_world).action_map) {
    EXPECT_EQ((*ids)[index++], info.id);
  }
  EXPECT_EQ(Map(test_world).action_map.size(), count_before);
}

TEST(ActionNegotiationTest, UnknownActionGetsTheNextFreeIdAndStaysOutOfTheConfig) {
  Z13TestWorld test_world;
  const ActionInfo::IdType first_free = MaxId(Map(test_world)) + 1;

  const auto ids = RegisterRemoteActions(Map(test_world), std::vector {Extra()});

  ASSERT_TRUE(ids.has_value()) << ids.error();
  EXPECT_EQ(ids->front(), first_free);
  const auto found = InputConfigLoader::FindActionId(Map(test_world).action_map, kExtraEnum, kExtraEnumValue);
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(*found, first_free);

  InputConfig config;
  InputConfigLoader::SetDefaults(config, Map(test_world));
  const auto json = InputConfigLoader::SerializeConfig(config, Map(test_world));
  ASSERT_TRUE(json.has_value());
  EXPECT_EQ(json->find(kExtraValue), std::string::npos) << "a remote action must not reach the saved input config";
}

TEST(ActionNegotiationTest, ASecondClientWithTheSameActionGetsTheSameId) {
  Z13TestWorld test_world;
  const auto first = RegisterRemoteActions(Map(test_world), std::vector {Extra()});
  const auto second = RegisterRemoteActions(Map(test_world), std::vector {Extra()});

  ASSERT_TRUE(first.has_value() && second.has_value());
  EXPECT_EQ(*first, *second);
}

TEST(ActionNegotiationTest, ADifferentActionNeverReusesAnId) {
  Z13TestWorld test_world;
  const auto first = RegisterRemoteActions(Map(test_world), std::vector {Extra("ONE", 701)});
  const auto second = RegisterRemoteActions(Map(test_world), std::vector {Extra("TWO", 702)});

  ASSERT_TRUE(first.has_value() && second.has_value());
  EXPECT_NE(first->front(), second->front());
}

TEST(ActionNegotiationTest, ADifferentEnumValueForAKnownNameKeepsTheServersId) {
  Z13TestWorld test_world;
  const auto first = RegisterRemoteActions(Map(test_world), std::vector {Extra()});
  const size_t count_before = Map(test_world).action_map.size();

  const auto again = RegisterRemoteActions(Map(test_world), std::vector {Extra(std::string(kExtraValue), 999)});

  ASSERT_TRUE(first.has_value() && again.has_value());
  EXPECT_EQ(*first, *again);
  EXPECT_EQ(Map(test_world).action_map.size(), count_before);
}

TEST(ActionNegotiationTest, ATakenEnumValueMovesToTheNextFreeOne) {
  Z13TestWorld test_world;
  ASSERT_TRUE(RegisterRemoteActions(Map(test_world), std::vector {Extra()}).has_value());

  const auto ids = RegisterRemoteActions(Map(test_world), std::vector {Extra("CLASH", kExtraEnumValue)});

  ASSERT_TRUE(ids.has_value()) << ids.error();
  const auto found = InputConfigLoader::FindActionId(Map(test_world).action_map, kExtraEnum, kExtraEnumValue + 1);
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(*found, ids->front());
}

TEST(ActionNegotiationTest, AnInvalidNameRefusesTheWholeListAndRegistersNothing) {
  Z13TestWorld test_world;
  const size_t count_before = Map(test_world).action_map.size();

  EXPECT_FALSE(RegisterRemoteActions(Map(test_world), std::vector {Extra("FRESH", 800), Extra("")}).has_value());
  EXPECT_FALSE(RegisterRemoteActions(
      Map(test_world), std::vector {Extra("FRESH", 800), Extra(std::string(kMaxActionNameLength + 1, 'x'))}).has_value());

  EXPECT_EQ(Map(test_world).action_map.size(), count_before);
}

std::vector<ActionDescriptor> ManyActions(size_t count, ActionInfo::EnumValueType first_value) {
  std::vector<ActionDescriptor> actions;
  for (size_t i = 0; i < count; ++i) {
    actions.push_back(Extra(std::format("A{}", first_value + static_cast<ActionInfo::EnumValueType>(i)), first_value + static_cast<ActionInfo::EnumValueType>(i)));
  }
  return actions;
}

TEST(ActionNegotiationTest, ANewActionListedTwiceInOneHelloGetsOneId) {
  Z13TestWorld test_world;
  const size_t count_before = Map(test_world).action_map.size();

  const auto ids = RegisterRemoteActions(Map(test_world), std::vector {Extra(), Extra()});

  ASSERT_TRUE(ids.has_value()) << ids.error();
  EXPECT_EQ((*ids)[0], (*ids)[1]);
  EXPECT_EQ(Map(test_world).action_map.size(), count_before + 1);
}

TEST(ActionNegotiationTest, ASingleClientCannotListMoreThanTheLimit) {
  Z13TestWorld test_world;
  const size_t count_before = Map(test_world).action_map.size();

  EXPECT_TRUE(RegisterRemoteActions(Map(test_world), ManyActions(kMaxActionsPerClient, 1000)).has_value());
  EXPECT_FALSE(RegisterRemoteActions(Map(test_world), ManyActions(kMaxActionsPerClient + 1, 5000)).has_value());
  EXPECT_EQ(Map(test_world).action_map.size(), count_before + kMaxActionsPerClient);
}

TEST(ActionNegotiationTest, ClientsTogetherCannotRegisterMoreThanTheServerLimit) {
  Z13TestWorld test_world;
  for (size_t registered = 0; registered < kMaxRemoteActions; registered += kMaxActionsPerClient) {
    const size_t batch = std::min(kMaxActionsPerClient, kMaxRemoteActions - registered);
    ASSERT_TRUE(RegisterRemoteActions(Map(test_world), ManyActions(batch, 1000 + static_cast<ActionInfo::EnumValueType>(registered))).has_value());
  }
  const size_t count_at_limit = Map(test_world).action_map.size();

  EXPECT_FALSE(RegisterRemoteActions(Map(test_world), std::vector {Extra("ONE_TOO_MANY", 90000)}).has_value());
  EXPECT_EQ(Map(test_world).action_map.size(), count_at_limit);

  // Already registered actions keep working for new clients.
  EXPECT_TRUE(RegisterRemoteActions(Map(test_world), ManyActions(kMaxActionsPerClient, 1000)).has_value());
}

TEST(ActionNegotiationTest, AdoptingIdsRenumbersTheMapAndKeepsTheKeyBindings) {
  Z13TestWorld test_world;
  const auto before = DescribeActions(Map(test_world));
  const auto bindings_before = test_world.World().get<InputConfig>().keycode_binding.size();
  std::vector<uint32_t> shifted(before.size());
  std::iota(shifted.begin(), shifted.end(), kShift);

  const auto adopted = AdoptActionIds(test_world.World(), shifted);

  ASSERT_TRUE(adopted.has_value()) << adopted.error();
  const auto& by_id = Map(test_world).action_map.get<ActionMap::IdTag>();
  EXPECT_EQ(by_id.begin()->id, kShift);
  EXPECT_EQ(Map(test_world).action_map.size(), before.size());
  const auto after = DescribeActions(Map(test_world));
  for (size_t i = 0; i < before.size(); ++i) {
    EXPECT_EQ(after[i].value_name, before[i].value_name) << "renumbering reordered the actions";
  }

  const InputConfig& config = test_world.World().get<InputConfig>();
  EXPECT_EQ(config.keycode_binding.size(), bindings_before);
  for (const auto& binding : config.keycode_binding) {
    EXPECT_NE(by_id.find(binding.action_id), by_id.end()) << "a key binding points at an id the map no longer has";
  }
}

TEST(ActionNegotiationTest, AdoptingAWrongSizedOrRepeatedListChangesNothing) {
  Z13TestWorld test_world;
  const size_t count = Map(test_world).action_map.size();
  const ActionInfo::IdType first_id = Map(test_world).action_map.get<ActionMap::IdTag>().begin()->id;

  EXPECT_FALSE(AdoptActionIds(test_world.World(), std::vector<uint32_t>(count + 1, 1)).has_value());
  EXPECT_FALSE(AdoptActionIds(test_world.World(), std::vector<uint32_t>(count, 1)).has_value());

  EXPECT_EQ(Map(test_world).action_map.size(), count);
  EXPECT_EQ(Map(test_world).action_map.get<ActionMap::IdTag>().begin()->id, first_id);
}

}  // namespace
}  // namespace z13::gameplay::input
