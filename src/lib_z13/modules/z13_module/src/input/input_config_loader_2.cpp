#include <z13_module/input/input_config_loader_2.h>

#include <algorithm>
#include <fstream>
#include <filesystem>

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/join.hpp>

#include <flatbuffers/idl.h>
#include <flatbuffers/flatbuffers.h>

#include <lib_core/log.h>
#include <z13_module/tools/z13_environment.h>
#include <z13/components/input.h>

#include <input_config_generated.h>

namespace z13::gameplay::input {

static constexpr auto kActionNameSeparator = ":";

bool InputConfigLoader2::LoadConfig(
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

  flatbuffers::Parser parser;

  const auto* input_config_schema = reflection::GetSchema(z13::fbs::input::InputConfigBinarySchema::data());
  if (!parser.Deserialize(input_config_schema)) {
    LOG_ERROR("InputConfigLoader2::LoadConfig: Failed to deserialize binary schema");
    return false;
  }

  if (!parser.Parse(json_input.c_str())) {
    LOG_ERROR("InputConfigLoader2::LoadConfig: Cannot parse json: ", parser.error_);
    return false;
  }

  uint8_t* buf = parser.builder_.GetBufferPointer();
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
        "InputConfigLoader2::LoadConfig: Wrong action name format. Value is '{}'",
        action_name
      );
      continue;
    }
    auto it = enum_action_names.find(std::make_tuple(action_tokens[0], action_tokens[1]));
    if (it == enum_action_names.end()) {
      LOG_ERROR(
        "InputConfigLoader2::LoadConfig: Cannot find registered action '{}'",
        action_name
      );
      continue;
    }

    input_config.keycode_binding.emplace(
      z13::input::KeyCodeAction {
        .keycode = key_code,
        .action_group = it->group_name,
        .display_text = it->display_text,
        .action_id = it->id,
      }
    );
  }

  return true;
}

bool InputConfigLoader2::SaveConfig(
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
        throw std::runtime_error(fmt::format("InputConfigLoader2::SaveConfig: Cannot find action with id = {}", keycode_action.action_id));
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
    LOG_ERROR("InputConfigLoader2::SaveConfig: Failed to deserialize binary schema");
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
    LOG_ERROR("InputConfigLoader2::SaveConfig: Failed to serialize data: {}", res);
    return false;
  }

  std::filesystem::create_directory(z13::tools::environment::GetGameDataDirectory());

  auto config_file_path = z13::tools::environment::GetGameInputConfigJsonPath2();
  std::ofstream output_file(config_file_path);
  if (!output_file.is_open()) {
    LOG_ERROR("InputConfigLoader2::SaveConfig: cannot open json file for write '{}'", config_file_path.string());
    return false;
  }

  output_file << json_output;
  output_file.close();

  return true;
}

void InputConfigLoader2::SetDefaults(
    z13::input::InputConfig& input_config,
    const z13::input::ActionMap& action_map) {
  input_config.keycode_binding.clear();

  static constexpr std::string_view kDefaultActionName = "z13.fbs.actions.Action";
  auto add_binding = [&](auto action, auto key_code) {
    auto action_id = static_cast<z13::input::ActionInfo::ValueType>(action);
    const auto& action_values = action_map.action_map.get<z13::input::ActionMap::EnumValueTag>(); {
      auto it = action_values.find(std::make_tuple(
        kDefaultActionName,
        action_id
      ));
      if (it == action_values.end()) {
        LOG_CRITICAL(
          "InputConfigLoader2::SetDefaults: Cannot get id for action enum = '{}', action id = {}",
          kDefaultActionName,
          action_id
        );
      }

      input_config.keycode_binding.emplace(
        z13::input::KeyCodeAction {
          .keycode = key_code,
          .action_group = it->group_name,
          .display_text = it->display_text,
          .action_id = it->id,
        }
      );
    }
  };

  add_binding(z13::fbs::actions::Action::MOVE_FORWARD, z13::fbs::input::Keycode::KEY_W);
  add_binding(z13::fbs::actions::Action::MOVE_BACKWARD, z13::fbs::input::Keycode::KEY_S);
  add_binding(z13::fbs::actions::Action::MOVE_LEFT, z13::fbs::input::Keycode::KEY_A);
  add_binding(z13::fbs::actions::Action::MOVE_RIGHT, z13::fbs::input::Keycode::KEY_D);
  add_binding(z13::fbs::actions::Action::JUMP, z13::fbs::input::Keycode::KEY_SPACE);
  add_binding(z13::fbs::actions::Action::CROUCH, z13::fbs::input::Keycode::KEY_LCTRL);
  add_binding(z13::fbs::actions::Action::ACTION_1, z13::fbs::input::Keycode::MOUSE_BUTTON_LEFT);
}

void InputConfigLoader2::Clear(z13::input::InputConfig& input_config) {
  input_config = z13::input::InputConfig();
}

}  // namespace z13::gameplay::input
