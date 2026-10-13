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

#include <cstdint>
#include <deque>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <boost/multi_index/composite_key.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

#include <input_config_generated.h>
#include <z13/components/action_id.h>

namespace z13::input {

namespace bmi = boost::multi_index;

struct FlatbufferBinarySchema {
  std::span<const uint8_t> binary_schema;
};

struct ActionInfo {
  using EnumValueType = int64_t;

  const std::string_view enum_name;
  const std::string_view value_name;
  const std::string_view group_name;
  const std::string_view display_text;
  const std::vector<z13::fbs::input::Keycode> default_keycodes;
  const bool absolute {};  // see common.fbs
  const EnumValueType enum_value {};
  const ActionId id {};
};

constexpr std::string_view kRemoteActionGroup = "Remote";

struct ActionMap {
  using Singleton = void;
  struct ActionNameTag;
  struct EnumNameTag;
  struct EnumActionNameTag;
  struct GroupNameTag;
  struct IdTag;
  struct EnumValueTag;
  struct EnumNameEnumValueTag;

  using ActionMapContainer = bmi::multi_index_container<
    ActionInfo,
    bmi::indexed_by<
      bmi::sequenced<>,
      bmi::ordered_unique<
        bmi::tag<EnumActionNameTag>,
        bmi::composite_key<
          ActionInfo,
          bmi::member<ActionInfo, decltype(ActionInfo::enum_name), &ActionInfo::enum_name>,
          bmi::member<ActionInfo, decltype(ActionInfo::value_name), &ActionInfo::value_name>
        >
      >,
      bmi::ordered_non_unique<
        bmi::tag<ActionNameTag>,
        bmi::member<ActionInfo, decltype(ActionInfo::value_name), &ActionInfo::value_name>
      >,
      bmi::ordered_non_unique<
        bmi::tag<EnumNameTag>,
        bmi::member<ActionInfo, decltype(ActionInfo::enum_name), &ActionInfo::enum_name>
      >,
      bmi::ordered_non_unique<
        bmi::tag<GroupNameTag>,
        bmi::member<ActionInfo, decltype(ActionInfo::group_name), &ActionInfo::group_name>
      >,
      bmi::ordered_unique<
        bmi::tag<IdTag>,
        bmi::member<ActionInfo, decltype(ActionInfo::id), &ActionInfo::id>
      >,
      bmi::ordered_non_unique<
        bmi::tag<EnumValueTag>,
        bmi::member<ActionInfo, decltype(ActionInfo::enum_value), &ActionInfo::enum_value>
      >,
      bmi::ordered_unique<
        bmi::tag<EnumNameEnumValueTag>,
        bmi::composite_key<
          ActionInfo,
          bmi::member<ActionInfo, decltype(ActionInfo::enum_name), &ActionInfo::enum_name>,
          bmi::member<ActionInfo, decltype(ActionInfo::enum_value), &ActionInfo::enum_value>
        >
      >
    >
  >;

  ActionMapContainer action_map;
  // Deque: the name views of registered actions must stay valid.
  std::deque<std::string> owned_names;
};

// Default action group every local player listens to (movement, look).
// todo: убрать константу и брать из z13.fbs.actions.Action.action_group
constexpr std::string_view kControlActionGroup = "Control";

}  // namespace z13::input
