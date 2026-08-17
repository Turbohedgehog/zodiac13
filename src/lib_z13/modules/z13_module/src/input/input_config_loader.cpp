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

// #include <z13_module/input/input_config_loader.h>

module;

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/composite_key.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/identity.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/container/flat_map.hpp>

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/join.hpp>

#include <flatbuffers/idl.h>
#include <flatbuffers/flatbuffers.h>

#include <lib_core/log.h>

#include <input_config_generated.h>

module z13.gameplay.input;

import <algorithm>;
import <fstream>;
import <filesystem>;

import z13.tools.environment;
import z13.input;

namespace z13::gameplay::input {

static constexpr std::string_view kActionNameSeparator = ":";
static constexpr std::string_view kActionAttributeName = "action";
static constexpr std::string_view kActionGroupAttributeName = "action_group";
static constexpr std::string_view kDefaultActionGroupName = "DefaultGroup";
static constexpr std::string_view kEmptyDisplayTextName = "display_text";
static constexpr std::string_view kEmptyDisplayText = "";
static constexpr std::string_view kDefaultKeycodes = "default_keycodes";
static constexpr std::string_view kDefaultKeycodesSeparators = " ,;";

std::vector<z13::fbs::input::Keycode> ExtractDefaultKeycodes(
    std::string_view keycodes_value) {
  std::vector<std::string_view> key_tokens;
  boost::split(key_tokens, keycodes_value, boost::is_any_of(kDefaultKeycodesSeparators));
  if (key_tokens.empty()) {
    return {};
  }
  
  const char** keycode_names = const_cast<const char**>(z13::fbs::input::EnumNamesKeycode());
  std::vector<z13::fbs::input::Keycode> keycodes;
  std::transform(
    key_tokens.begin(),
    key_tokens.end(),
    std::back_inserter(keycodes),
    [&keycode_names](auto key_token) {
      auto keycode_enum_idx = flatbuffers::LookupEnum(keycode_names, key_token.data());
      if (keycode_enum_idx < 0) {
        LOG_CRITICAL("ExtractDefaultKeycodes: Cannot find key binding for token {}", key_token);
        keycode_enum_idx = 0;
      }

      return z13::fbs::input::EnumValuesKeycode()[keycode_enum_idx];
    }
  );

  return keycodes;
}

bool InputConfigLoader::LoadConfig(
    z13::input::InputConfig& input_config,
    const z13::input::ActionMap& action_map) {
  auto config_file_path = z13::tools::environment::GetGameInputConfigJsonPath2();
  if (!std::filesystem::exists(config_file_path)) {
    return false;
  }

  std::ifstream json_file(config_file_path);
  if (!json_file.is_open()) {
    return false;
  }

  std::string json_input(
      (std::istreambuf_iterator<char>(json_file)),
      std::istreambuf_iterator<char>());
  json_file.close();

  flatbuffers::IDLOptions idl_options;
  idl_options.skip_unexpected_fields_in_json = true;
  flatbuffers::Parser parser(idl_options);

  const auto* input_config_schema = reflection::GetSchema(z13::fbs::input::InputConfigBinarySchema::data());
  if (!parser.Deserialize(input_config_schema)) {
    LOG_ERROR("InputConfigLoader::LoadConfig: Failed to deserialize binary schema");
    return false;
  }

  if (!parser.Parse(json_input.c_str())) {
    LOG_ERROR("InputConfigLoader::LoadConfig: Cannot parse json: ", parser.error_);
    return false;
  }

  auto* buf = parser.builder_.GetBufferPointer();
  z13::fbs::input::InputConfigT input_config_msg;
  z13::fbs::input::GetInputConfig(buf)->UnPackTo(&input_config_msg);

  input_config.keycode_binding.clear();

  input_config.mouse_sensitivity = input_config_msg.mouse_config->mouse_sensitivity;
  input_config.invert_x = input_config_msg.mouse_config->invert_x;
  input_config.invert_y = input_config_msg.mouse_config->invert_y;

  std::vector<std::string> action_tokens;

  const auto& enum_action_names = action_map.action_map.get<z13::input::ActionMap::EnumActionNameTag>();

  for (const auto& action_binding : input_config_msg.action_bindings) {
    const auto& action_name = action_binding->action_name;
    boost::split(action_tokens, action_name, boost::is_any_of(kActionNameSeparator));
    const auto& key_code = action_binding->key_code;

    if (action_tokens.size() != 2) {
      LOG_ERROR(
        "InputConfigLoader::LoadConfig: Wrong action name format. Value is '{}'",
        action_name
      );
      continue;
    }
    auto it = enum_action_names.find(std::make_tuple(action_tokens[0], action_tokens[1]));
    if (it == enum_action_names.end()) {
      LOG_ERROR(
        "InputConfigLoader::LoadConfig: Cannot find registered action '{}'",
        action_name
      );
      continue;
    }

    input_config.keycode_binding.emplace(
      z13::input::KeyCodeAction {
        .keycode = key_code,
        .action_group = it->group_name,
        .action_id = it->id,
      }
    );
  }

  return true;
}

bool InputConfigLoader::SaveConfig(
    const z13::input::InputConfig& input_config,
    const z13::input::ActionMap& action_map) {
  z13::fbs::input::InputConfigT input_config_msg;
  std::transform(
    input_config.keycode_binding.begin(),
    input_config.keycode_binding.end(),
    std::back_inserter(input_config_msg.action_bindings),
    [&action_map](const auto& keycode_action) {
      auto ab = std::make_unique<z13::fbs::input::ActionBindingT>();
      const auto& id_map = action_map.action_map.get<z13::input::ActionMap::IdTag>();
      auto it = id_map.find(keycode_action.action_id);
      if (it == id_map.end()) {
        throw std::runtime_error(fmt::format("InputConfigLoader::SaveConfig: Cannot find action with id = {}", keycode_action.action_id));
      }

      ab->action_name = fmt::format("{}{}{}", it->enum_name, kActionNameSeparator, it->value_name);
      ab->key_code = keycode_action.keycode;

      return ab;
    }
  );

  auto mouse_config = std::make_unique<z13::fbs::input::MouseConfigT>();
  mouse_config->mouse_sensitivity = input_config.mouse_sensitivity;
  mouse_config->invert_x = input_config.invert_x;
  mouse_config->invert_y = input_config.invert_y;

  input_config_msg.mouse_config = std::move(mouse_config);

  flatbuffers::Parser parser;
  const auto* input_config_schema = reflection::GetSchema(z13::fbs::input::InputConfigBinarySchema::data());
  if (!parser.Deserialize(input_config_schema)) {
    LOG_ERROR("InputConfigLoader::SaveConfig: Failed to deserialize binary schema");
    return false;
  }

  flatbuffers::FlatBufferBuilder builder;
  auto offset = z13::fbs::input::InputConfig::Pack(builder, &input_config_msg);
  builder.Finish(offset);

  uint8_t* buf = builder.GetBufferPointer();
  size_t size = builder.GetSize();

  std::string json_output;
  parser.opts.indent_step = 2;
  parser.opts.output_default_scalars_in_json = true;
  parser.opts.strict_json = true;
  if (const auto* res = flatbuffers::GenerateText(parser, builder.GetBufferPointer(), &json_output); res) {
    LOG_ERROR("InputConfigLoader::SaveConfig: Failed to serialize data: {}", res);
    return false;
  }

  std::filesystem::create_directory(z13::tools::environment::GetGameDataDirectory());

  auto config_file_path = z13::tools::environment::GetGameInputConfigJsonPath2();
  std::ofstream output_file(config_file_path);
  if (!output_file.is_open()) {
    LOG_ERROR("InputConfigLoader::SaveConfig: cannot open json file for write '{}'", config_file_path.string());
    return false;
  }

  output_file << json_output;
  output_file.close();

  return true;
}

void InputConfigLoader::SetDefaults(
    z13::input::InputConfig& input_config,
    const z13::input::ActionMap& action_map) {
  input_config.keycode_binding.clear();

  for (const auto& action_info : action_map.action_map) {
    std::transform(
      action_info.default_keycodes.begin(),
      action_info.default_keycodes.end(),
      std::inserter(input_config.keycode_binding, input_config.keycode_binding.end()),
      [&action_info](auto key_code) {
        return z13::input::KeyCodeAction {
          .keycode = key_code,
          .action_group = action_info.group_name,
          .action_id = action_info.id,
        };
      }
    );
  }
}

void InputConfigLoader::Clear(z13::input::InputConfig& input_config) {
  input_config = z13::input::InputConfig();
}

void InputConfigLoader::AppendFlatbufActionsFromBinarySchema(
    const z13::input::FlatbufferBinarySchema& lookup_actions,
    z13::input::ActionMap& action_map) {

  const auto* input_config_schema = reflection::GetSchema(lookup_actions.binary_schema.data());
  const auto* enums = input_config_schema->enums();
  if (!enums) {
    LOG_ERROR("InputConfigLoader::AppendFlatbufActionsFromBinarySchema: no enums in schema");
    return;
  }

  for (const auto* en : *enums) {
    const auto* attributes = en->attributes();
    if (!attributes) {
      continue;
    }

    if (!attributes->LookupByKey(kActionAttributeName)) {
      continue;
    }

    const auto* values = en->values();
    if (!values) {
      continue;
    }

    auto enum_name = en->name()->string_view();
    std::string_view group_name = kDefaultActionGroupName;

    const auto* action_group_name = attributes->LookupByKey(kActionGroupAttributeName);
    if (action_group_name) {
      group_name = action_group_name->value()->string_view();
    }

    for (const auto* value : *values) {
      auto value_group = group_name;
      auto display_text = kEmptyDisplayText;
      std::vector<z13::fbs::input::Keycode> default_keycodes;
      if (const auto* value_attributes = value->attributes()) {
        if (const auto* value_group_name = value_attributes->LookupByKey(kActionGroupAttributeName)) {
          value_group = value_group_name->value()->string_view();
        }

        if (const auto* display_text_attribute = value_attributes->LookupByKey(kEmptyDisplayTextName)) {
          display_text = display_text_attribute->value()->string_view();
        }

        if (const auto* default_keycodes_attribute = value_attributes->LookupByKey(kDefaultKeycodes)) {
          default_keycodes = ExtractDefaultKeycodes(default_keycodes_attribute->value()->string_view());
        }
      }

      action_map.action_map.insert(
        z13::input::ActionInfo {
          .enum_name = enum_name,
          .value_name = value->name()->string_view(),
          .group_name = value_group,
          .display_text = display_text,
          .default_keycodes = default_keycodes,
          .enum_value = value->value(),
          .id = action_map.action_map.size(),
        }
      );
    }
  }
}

std::optional<z13::input::ActionInfo::IdType> InputConfigLoader::FindActionId(
  const z13::input::ActionMap::ActionMapContainer& action_map_container,
  std::string_view action_enum_name,
  z13::input::ActionInfo::EnumValueType action_enum_value
) {
  const auto& enum_to_value_map = action_map_container.get<z13::input::ActionMap::EnumNameEnumValueTag>();
  auto it = enum_to_value_map.find(std::make_tuple(action_enum_name, action_enum_value));
  return it != enum_to_value_map.end() ? std::optional<z13::input::ActionInfo::IdType>(it->id) : std::nullopt;
}

}  // namespace z13::gameplay::input
