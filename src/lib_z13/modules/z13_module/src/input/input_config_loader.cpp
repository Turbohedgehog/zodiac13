#include <z13_module/input/input_config_loader.h>

#include <algorithm>
#include <fstream>
#include <filesystem>

#include <flatbuffers/idl.h>

#include <lib_core/log.h>
#include <z13_module/tools/z13_environment.h>
#include <z13/components/input.h>

#include <input_config_generated.h>

namespace z13::gameplay::input {

bool InputConfigLoader::LoadConfig(z13::input::InputConfig& input_config) {
  auto config_file_path = z13::tools::environment::GetGameInputConfigJsonPath();
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
    LOG_ERROR("InputConfigLoader::LoadConfig: Failed to deserialize binary schema");
    return false;
  }

  if (!parser.Parse(json_input.c_str())) {
    LOG_ERROR("InputConfigLoader::LoadConfig: Cannot parse json: ", parser.error_);
    return false;
  }

  uint8_t* buf = parser.builder_.GetBufferPointer();
  z13::fbs::input::InputConfigT input_config_msg;
  z13::fbs::input::GetInputConfig(buf)->UnPackTo(&input_config_msg);

  Clear(input_config);

  for (const auto& action_binding : input_config_msg.action_bindings) {
    auto action = action_binding->action;
    auto it = std::find_if(
        input_config.action_bindings.begin(),
        input_config.action_bindings.end(),
        [&action] (const auto& binding) {
          return binding.action == action;
        }
    );

    if (it == input_config.action_bindings.end()) {
      input_config.action_bindings.emplace_back(z13::input::ActionBinding{.action = action});
      it = input_config.action_bindings.end() - 1;
    }

    auto& keys = it->keys;
    std::transform(
      action_binding->key_codes.begin(),
      action_binding->key_codes.end(),
      std::back_inserter(keys),
      [] (const auto& key) { return static_cast<z13::fbs::input::Keycode>(key); }
    );
  }

  input_config.mouse_sensitivity = input_config_msg.mouse_config->mouse_sensitivity;
  input_config.invert_x = input_config_msg.mouse_config->invert_x;
  input_config.invert_y = input_config_msg.mouse_config->invert_y;

  input_config.code_to_action.clear();
  for (const auto& action_binding : input_config.action_bindings) {
    for (const auto& key : action_binding.keys) {
      input_config.code_to_action[key] = action_binding.action;
    }
  }

  return true;
}

bool InputConfigLoader::SaveConfig(const z13::input::InputConfig& input_config) {
  z13::fbs::input::InputConfigT input_config_msg;
  std::transform(
    input_config.action_bindings.begin(),
    input_config.action_bindings.end(),
    std::back_inserter(input_config_msg.action_bindings),
    [](const auto& action_binding) {
      auto ab = std::make_unique<z13::fbs::input::ActionBindingT>();
      ab->action = action_binding.action;
      std::transform(
        action_binding.keys.begin(),
        action_binding.keys.end(),
        std::back_inserter(ab->key_codes),
        [](auto key) { return key; }
      );
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

  auto config_file_path = z13::tools::environment::GetGameInputConfigJsonPath();
  std::ofstream output_file(config_file_path);
  if (!output_file.is_open()) {
    LOG_ERROR("InputConfigLoader::SaveConfig: cannot open json file for write '{}'", config_file_path.string());
    return false;
  }

  output_file << json_output;
  output_file.close();

  return false;
}

void InputConfigLoader::Clear(z13::input::InputConfig& input_config) {
  input_config = z13::input::InputConfig();
}

void InputConfigLoader::SetDefaults(z13::input::InputConfig& input_config) {
  Clear(input_config);

  input_config.action_bindings.push_back({z13::fbs::input::Action::MOVE_FORWARD, {z13::fbs::input::Keycode::KEY_W}});
  input_config.action_bindings.push_back({z13::fbs::input::Action::MOVE_BACKWARD, {z13::fbs::input::Keycode::KEY_S}});
  input_config.action_bindings.push_back({z13::fbs::input::Action::MOVE_LEFT, {z13::fbs::input::Keycode::KEY_A}});
  input_config.action_bindings.push_back({z13::fbs::input::Action::MOVE_RIGHT, {z13::fbs::input::Keycode::KEY_D}});
  input_config.action_bindings.push_back({z13::fbs::input::Action::JUMP, {z13::fbs::input::Keycode::KEY_SPACE}});
  input_config.action_bindings.push_back({z13::fbs::input::Action::CROUCH, {z13::fbs::input::Keycode::KEY_LCTRL}});
}

}  // namespace z13::gameplay
