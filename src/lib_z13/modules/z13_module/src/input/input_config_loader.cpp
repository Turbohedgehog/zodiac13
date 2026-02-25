#include <z13_module/input/input_config_loader.h>

#include <algorithm>
#include <fstream>
#include <filesystem>

#include <google/protobuf/util/json_util.h>

#include <lib_core/log.h>
#include <z13_module/tools/z13_environment.h>
#include <z13_module/components/input.h>

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
  google::protobuf::util::JsonParseOptions options {
    .ignore_unknown_fields = true,
  };

  z13::proto::input::InputConfig input_config_msg;
  auto status = google::protobuf::json::JsonStringToMessage(json_input, &input_config_msg, options);
  if (!status.ok()) {
    LOG_ERROR("InputConfigLoader::LoadConfig: cannot load file '{}' by reason: {}", config_file_path.string(), status.message());
    return false;
  }
 
  Clear(input_config);

  for (const auto& action_binding : input_config_msg.action_bindings()) {
    auto action = action_binding.action();
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
      action_binding.key_codes().begin(),
      action_binding.key_codes().end(),
      std::back_inserter(keys),
      [] (const auto& key) { return static_cast<z13::proto::input::Keyboard::Code>(key); }
    );
  }

  input_config.mouse_sensitivity = input_config_msg.mouse_config().mouse_sensitivity();
  input_config.invert_x = input_config_msg.mouse_config().invert_x();
  input_config.invert_y = input_config_msg.mouse_config().invert_y();

  input_config.code_to_action.clear();
  for (const auto& action_binding : input_config.action_bindings) {
    for (const auto& key : action_binding.keys) {
      input_config.code_to_action[key] = action_binding.action;
    }
  }

  return true;
}

bool InputConfigLoader::SaveConfig(const z13::input::InputConfig& input_config) {
  z13::proto::input::InputConfig input_config_msg;
  for (const auto& key_binding: input_config.action_bindings) {
    auto* action_binding = input_config_msg.add_action_bindings();
    action_binding->set_action(key_binding.action);
    for (auto key : key_binding.keys) {
      action_binding->add_key_codes(key);
    }  
  }

  auto* mouse_config = input_config_msg.mutable_mouse_config();
  mouse_config->set_mouse_sensitivity(input_config.mouse_sensitivity);
  mouse_config->set_invert_x(input_config.invert_x);
  mouse_config->set_invert_y(input_config.invert_y);

  std::string json_output;
  google::protobuf::util::JsonPrintOptions options {
    .add_whitespace = true,
    .always_print_fields_with_no_presence = true,
  };
  
  auto status = google::protobuf::json::MessageToJsonString(input_config_msg, &json_output, options);
  if (!status.ok()) {
    LOG_ERROR("InputConfigLoader::SaveConfig: cannot dump proto to json by reason: {}", status.message());
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

  input_config.action_bindings.push_back({z13::proto::input::Action::MOVE_FORWARD, {z13::proto::input::Keyboard::KEY_W}});
  input_config.action_bindings.push_back({z13::proto::input::Action::MOVE_BACKWARD, {z13::proto::input::Keyboard::KEY_S}});
  input_config.action_bindings.push_back({z13::proto::input::Action::MOVE_LEFT, {z13::proto::input::Keyboard::KEY_A}});
  input_config.action_bindings.push_back({z13::proto::input::Action::MOVE_RIGHT, {z13::proto::input::Keyboard::KEY_D}});
}

}  // namespace z13::gameplay
