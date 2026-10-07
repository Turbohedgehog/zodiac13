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

#include <z13_launcher/settings_loader.h>

#include <format>

#include <flatbuffers/idl.h>
#include <flatbuffers/reflection.h>

#include <lib_core/utils/file_io.h>
#include <lib_core/utils/log.h>
#include <lib_core/utils/status.h>

namespace z13 {

namespace {

constexpr int kJsonIndentStep = 2;

Status WriteSettings(const std::filesystem::path& path, const Settings& settings) {
  const auto json = SerializeSettings(settings);
  if (!json) {
    return std::unexpected(json.error());
  }
  return WriteFile(path, *json);
}

}  // namespace

std::expected<Settings, std::string> ParseSettings(const std::string& json_text) {
  flatbuffers::Parser parser;
  if (!parser.Deserialize(reflection::GetSchema(fbs::settings::SettingsBinarySchema::data()))) {
    return std::unexpected("ParseSettings: failed to deserialize the binary schema");
  }
  if (!parser.Parse(json_text.c_str())) {
    return std::unexpected(std::format("ParseSettings: {}", parser.error_));
  }

  fbs::settings::SettingsT message;
  fbs::settings::GetSettings(parser.builder_.GetBufferPointer())->UnPackTo(&message);

  EnsureNestedSettings(message);
  if (const auto valid = ValidateSettings(message); !valid) {
    return std::unexpected(valid.error());
  }
  return message;
}

std::expected<std::string, std::string> SerializeSettings(const Settings& settings) {
  flatbuffers::Parser parser;
  if (!parser.Deserialize(reflection::GetSchema(fbs::settings::SettingsBinarySchema::data()))) {
    return std::unexpected("SerializeSettings: failed to deserialize the binary schema");
  }

  flatbuffers::FlatBufferBuilder builder;
  builder.Finish(fbs::settings::Settings::Pack(builder, &settings));

  parser.opts.indent_step = kJsonIndentStep;
  parser.opts.output_default_scalars_in_json = true;
  parser.opts.strict_json = true;
  std::string json;
  if (const char* error = flatbuffers::GenerateText(parser, builder.GetBufferPointer(), &json)) {
    return std::unexpected(std::format("SerializeSettings: {}", error));
  }
  return json;
}

std::expected<Settings, std::string> ReadSettings(const std::filesystem::path& config_path) {
  if (!std::filesystem::exists(config_path)) {
    Settings defaults = MakeSettings();
    if (const auto written = WriteSettings(config_path, defaults); !written) {
      log_warn("ReadSettings: not creating the settings file: {}", written.error());
    }
    return defaults;
  }

  const auto contents = ReadFile(config_path);
  if (!contents) {
    return std::unexpected(std::format("ReadSettings: {}", contents.error()));
  }
  return ParseSettings(*contents).transform_error([&config_path](const std::string& error) {
    return std::format("{} ({})", error, config_path.string());
  });
}

}  // namespace z13
