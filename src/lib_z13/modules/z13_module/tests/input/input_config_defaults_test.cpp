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

#include <optional>
#include <set>
#include <string>
#include <tuple>

#include <z13/components/input.h>
#include <z13_module/input/input_config_loader.h>

#include "../support/z13_test_world.h"

// A config file written before an action existed gets that action's default binding; an action
// the user unbound is saved as KEY_UNKNOWN and stays unbound.
namespace z13::gameplay::input {
namespace {

using Action = z13::fbs::actions::Action;
using Keycode = z13::fbs::input::Keycode;
constexpr std::string_view kActionsEnum = "z13.fbs.actions.Action";

constexpr std::string_view kConfigTemplate = R"({
  "mouse_config": {"mouse_sensitivity": 5, "invert_x": false, "invert_y": false},
  "action_bindings": [%s]
})";

std::string Config(const std::string& bindings) {
  std::string config(kConfigTemplate);
  config.replace(config.find("%s"), 2, bindings);
  return config;
}

std::string Binding(std::string_view action, std::string_view key) {
  return R"({"action_name": "z13.fbs.actions.Action:)" + std::string(action) + R"(", "key_code": ")" +
         std::string(key) + R"("})";
}

class InputConfigDefaultsTest : public ::testing::Test {
 protected:
  const z13::input::ActionMap& ActionMap() { return test_world_.World().get<z13::input::ActionMap>(); }

  z13::input::ActionInfo::IdType IdOf(Action action) {
    return InputConfigLoader::FindActionId(ActionMap().action_map, kActionsEnum, action).value();
  }

  std::optional<Keycode> KeyOf(const z13::input::InputConfig& config, Action action) {
    const auto& by_action = config.keycode_binding.get<z13::input::InputConfig::ActionIdTag>();
    const auto it = by_action.find(IdOf(action));
    return it == by_action.end() ? std::nullopt : std::optional<Keycode>(it->keycode);
  }

  using BindingSet = std::set<std::tuple<z13::input::ActionInfo::IdType, Keycode>>;

  static BindingSet Bindings(const z13::input::InputConfig& config) {
    BindingSet bindings;
    for (const auto& binding : config.keycode_binding) {
      bindings.emplace(binding.action_id, binding.keycode);
    }
    return bindings;
  }

  void Unbind(z13::input::InputConfig& config, Action action) {
    auto& by_action = config.keycode_binding.get<z13::input::InputConfig::ActionIdTag>();
    const auto range = by_action.equal_range(IdOf(action));
    by_action.erase(range.first, range.second);
  }

  z13::testing::Z13TestWorld test_world_;
};

TEST_F(InputConfigDefaultsTest, ActionsMissingFromTheFileGetTheirDefaultKeys) {
  z13::input::InputConfig config;
  const std::string json = Config(Binding("MOVE_FORWARD", "KEY_I"));

  ASSERT_TRUE(InputConfigLoader::LoadConfigFromJson(json, config, ActionMap()));

  EXPECT_EQ(KeyOf(config, Action::SAVE_SCENE), Keycode::KEY_F5);
  EXPECT_EQ(KeyOf(config, Action::LOAD_SCENE), Keycode::KEY_F9);
}

TEST_F(InputConfigDefaultsTest, BindingsFromTheFileAreKeptAndNotDoubled) {
  z13::input::InputConfig config;
  const std::string json = Config(Binding("MOVE_FORWARD", "KEY_I"));

  ASSERT_TRUE(InputConfigLoader::LoadConfigFromJson(json, config, ActionMap()));

  const auto& by_action = config.keycode_binding.get<z13::input::InputConfig::ActionIdTag>();
  EXPECT_EQ(KeyOf(config, Action::MOVE_FORWARD), Keycode::KEY_I);
  EXPECT_EQ(by_action.count(IdOf(Action::MOVE_FORWARD)), 1u);
}

TEST_F(InputConfigDefaultsTest, ADefaultKeyAlreadyTakenByAnotherActionIsSkipped) {
  z13::input::InputConfig config;
  const std::string json = Config(Binding("JUMP", "KEY_F5"));

  ASSERT_TRUE(InputConfigLoader::LoadConfigFromJson(json, config, ActionMap()));

  EXPECT_EQ(KeyOf(config, Action::JUMP), Keycode::KEY_F5);
  EXPECT_FALSE(KeyOf(config, Action::SAVE_SCENE).has_value());
  EXPECT_EQ(KeyOf(config, Action::LOAD_SCENE), Keycode::KEY_F9);
}

TEST_F(InputConfigDefaultsTest, AnActionSavedAsUnknownStaysUnboundAndBindsNoKey) {
  z13::input::InputConfig config;
  const std::string json = Config(Binding("MOVE_FORWARD", "KEY_UNKNOWN"));

  ASSERT_TRUE(InputConfigLoader::LoadConfigFromJson(json, config, ActionMap()));

  EXPECT_FALSE(KeyOf(config, Action::MOVE_FORWARD).has_value());
  EXPECT_EQ(config.keycode_binding.get<z13::input::InputConfig::KeycodeIdTag>().count(Keycode::KEY_UNKNOWN), 0u);
  EXPECT_EQ(KeyOf(config, Action::SAVE_SCENE), Keycode::KEY_F5);
}

TEST_F(InputConfigDefaultsTest, DefaultsSurviveASaveAndLoadRoundTrip) {
  z13::input::InputConfig defaults;
  InputConfigLoader::SetDefaults(defaults, ActionMap());
  const auto json = InputConfigLoader::SerializeConfig(defaults, ActionMap());
  ASSERT_TRUE(json.has_value());

  z13::input::InputConfig loaded;
  ASSERT_TRUE(InputConfigLoader::LoadConfigFromJson(*json, loaded, ActionMap()));

  EXPECT_EQ(Bindings(loaded), Bindings(defaults));
}

TEST_F(InputConfigDefaultsTest, AnActionUnboundByTheUserIsSavedAndStaysUnbound) {
  z13::input::InputConfig config;
  InputConfigLoader::SetDefaults(config, ActionMap());
  Unbind(config, Action::SAVE_SCENE);
  const BindingSet expected = Bindings(config);

  const auto json = InputConfigLoader::SerializeConfig(config, ActionMap());
  ASSERT_TRUE(json.has_value());
  EXPECT_NE(json->find("Action:SAVE_SCENE"), std::string::npos);

  z13::input::InputConfig loaded;
  ASSERT_TRUE(InputConfigLoader::LoadConfigFromJson(*json, loaded, ActionMap()));

  EXPECT_FALSE(KeyOf(loaded, Action::SAVE_SCENE).has_value());
  EXPECT_EQ(Bindings(loaded), expected);
}

}  // namespace
}  // namespace z13::gameplay::input
