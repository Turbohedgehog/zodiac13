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
#include <cmath>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/idl.h>
#include <flatbuffers/reflection.h>

#include <lib_core/config.h>
#include <lib_core/core.h>
#include <lib_core/schema_attributes.h>
#include <z13_settings/settings.h>

// The attribute machinery is generic; the game's settings schema is simply the richest
// schema at hand to exercise it on.
namespace z13 {
namespace {

const reflection::Schema& Schema() {
  return *reflection::GetSchema(fbs::settings::SettingsBinarySchema::data());
}

flatbuffers::FlatBufferBuilder Pack(const Settings& settings) {
  flatbuffers::FlatBufferBuilder builder;
  builder.ForceDefaults(true);
  builder.Finish(fbs::settings::Settings::Pack(builder, &settings));
  return builder;
}

const schema::CliOption* Find(const std::vector<schema::CliOption>& options, std::string_view path) {
  const auto found = std::ranges::find(options, path, &schema::CliOption::path);
  return found == options.end() ? nullptr : &*found;
}

std::expected<Settings, std::string> ApplyArguments(std::vector<std::string> arguments, const Settings& base) {
  Config config;
  if (const auto added = AddSettingsOptions(config); !added) {
    return std::unexpected(added.error());
  }
  std::string program = "z13_test";
  std::vector<char*> argv {program.data()};
  for (std::string& argument : arguments) {
    argv.push_back(argument.data());
  }
  if (const auto parsed = config.ParseCommandLineArguments(static_cast<int>(argv.size()), argv.data()); !parsed) {
    return std::unexpected(parsed.error());
  }
  return ApplyCliOverrides(config, base);
}

TEST(SchemaRangesTest, DefaultsAreInRange) {
  const auto packed = Pack(MakeSettings());

  const auto valid = schema::ValidateRanges(
      Schema(), *Schema().root_table(), *flatbuffers::GetRoot<flatbuffers::Table>(packed.GetBufferPointer()));

  EXPECT_TRUE(valid.has_value()) << valid.error();
}

TEST(SchemaRangesTest, ValueOutsideTheBoundsNamesItsPath) {
  Settings settings = MakeSettings();
  settings.core->fps = 0.;
  auto packed = Pack(settings);
  auto valid = schema::ValidateRanges(
      Schema(), *Schema().root_table(), *flatbuffers::GetRoot<flatbuffers::Table>(packed.GetBufferPointer()));
  ASSERT_FALSE(valid.has_value());
  EXPECT_NE(valid.error().find("core.fps"), std::string::npos) << valid.error();

  settings = MakeSettings();
  settings.core->fps = 5000.;
  packed = Pack(settings);
  valid = schema::ValidateRanges(
      Schema(), *Schema().root_table(), *flatbuffers::GetRoot<flatbuffers::Table>(packed.GetBufferPointer()));
  ASSERT_FALSE(valid.has_value());
  EXPECT_NE(valid.error().find("maximum"), std::string::npos) << valid.error();

  settings = MakeSettings();
  settings.net->send_interval_ticks = 0;
  packed = Pack(settings);
  valid = schema::ValidateRanges(
      Schema(), *Schema().root_table(), *flatbuffers::GetRoot<flatbuffers::Table>(packed.GetBufferPointer()));
  ASSERT_FALSE(valid.has_value());
  EXPECT_NE(valid.error().find("net.send_interval_ticks"), std::string::npos) << valid.error();
}

TEST(SchemaRangesTest, NonFiniteValuesAreRejected) {
  for (const double bad : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
    Settings settings = MakeSettings();
    settings.core->fps = bad;
    const auto packed = Pack(settings);

    const auto valid = schema::ValidateRanges(
        Schema(), *Schema().root_table(), *flatbuffers::GetRoot<flatbuffers::Table>(packed.GetBufferPointer()));

    ASSERT_FALSE(valid.has_value()) << bad;
    EXPECT_NE(valid.error().find("core.fps"), std::string::npos) << valid.error();
  }

  Settings smoothing = MakeSettings();
  smoothing.net->clock_offset_smoothing = std::numeric_limits<double>::quiet_NaN();
  EXPECT_FALSE(ValidateSettings(smoothing).has_value());
  Settings fps = MakeSettings();
  fps.core->fps = std::numeric_limits<double>::quiet_NaN();
  EXPECT_FALSE(ValidateSettings(fps).has_value());
}

// A schema built at run time, so a bound the game's own schemas never use can be tried.
class RuntimeSchemaTest : public ::testing::Test {
 protected:
  void Load(const std::string& schema_text) {
    flatbuffers::Parser schema_parser;
    ASSERT_TRUE(schema_parser.Parse(schema_text.c_str())) << schema_parser.error_;
    schema_parser.Serialize();
    binary_schema_.assign(
        schema_parser.builder_.GetBufferPointer(),
        schema_parser.builder_.GetBufferPointer() + schema_parser.builder_.GetSize());
    schema_ = reflection::GetSchema(binary_schema_.data());
  }

  std::expected<void, std::string> Validate(const std::string& json) {
    flatbuffers::Parser data_parser;
    if (!data_parser.Deserialize(binary_schema_.data(), binary_schema_.size())) {
      return std::unexpected("cannot load the schema");
    }
    if (!data_parser.Parse(json.c_str())) {
      return std::unexpected(data_parser.error_);
    }
    return schema::ValidateRanges(
        *schema_, *schema_->root_table(), *flatbuffers::GetRoot<flatbuffers::Table>(data_parser.builder_.GetBufferPointer()));
  }

  std::vector<uint8_t> binary_schema_;
  const reflection::Schema* schema_ {};
};

TEST_F(RuntimeSchemaTest, ZeroIsAnOrdinaryBound) {
  Load(R"(attribute "min"; attribute "max"; attribute "cli";
          table T { x: int = 5 (min: 0); y: int = -5 (max: 0); } root_type T;)");

  EXPECT_FALSE(Validate(R"({"x": -1})").has_value()) << "below a minimum of 0";
  EXPECT_TRUE(Validate(R"({"x": 0})").has_value());
  EXPECT_TRUE(Validate(R"({"x": 3, "y": 0})").has_value());
  EXPECT_FALSE(Validate(R"({"y": 1})").has_value()) << "above a maximum of 0";
}

TEST_F(RuntimeSchemaTest, ValuelessCliStillDerivesTheNameBesideZeroBounds) {
  Load(R"(attribute "min"; attribute "max"; attribute "cli";
          table T { max_late: int = 5 (min: 0, cli); } root_type T;)");

  const auto options = schema::CollectCliOptions(*schema_, *schema_->root_table());

  ASSERT_TRUE(options.has_value()) << options.error();
  ASSERT_EQ(options->size(), 1u);
  EXPECT_EQ(options->front().long_name, "max-late");
}

TEST(SchemaCliTest, ExplicitNamesGiveALongAndAShortForm) {
  const auto options = schema::CollectCliOptions(Schema(), *Schema().root_table());
  ASSERT_TRUE(options.has_value()) << options.error();

  const schema::CliOption* fps = Find(*options, "core.fps");
  ASSERT_NE(fps, nullptr);
  EXPECT_EQ(fps->long_name, "fps");
  EXPECT_EQ(fps->short_name, 'f');
  EXPECT_NE(fps->help.find("range [1, 1000]"), std::string::npos) << fps->help;
  EXPECT_NE(fps->help.find("default 60"), std::string::npos) << fps->help;
}

TEST(SchemaCliTest, BareAttributeDerivesTheLongNameFromTheField) {
  const auto options = schema::CollectCliOptions(Schema(), *Schema().root_table());
  ASSERT_TRUE(options.has_value()) << options.error();

  const schema::CliOption* late = Find(*options, "net.max_late_ticks");
  ASSERT_NE(late, nullptr);
  EXPECT_EQ(late->long_name, "max-late-ticks");
  EXPECT_FALSE(late->short_name.has_value());
  const schema::CliOption* timeout = Find(*options, "connect_timeout.limit");
  ASSERT_NE(timeout, nullptr);
  EXPECT_EQ(timeout->long_name, "connect-limit");
}

TEST(SchemaCliTest, SetsNestedNumericFieldsFromText) {
  Settings settings = MakeSettings();
  auto packed = Pack(settings);
  auto* table = flatbuffers::GetMutableRoot<flatbuffers::Table>(packed.GetBufferPointer());
  const reflection::Object& root = *Schema().root_table();

  ASSERT_TRUE(schema::SetFieldFromText(Schema(), root, *table, "core.fps", "30.5").has_value());
  ASSERT_TRUE(schema::SetFieldFromText(Schema(), root, *table, "net.max_late_ticks", "40").has_value());
  ASSERT_TRUE(schema::SetFieldFromText(Schema(), root, *table, "net.clock_catch_up_threshold_ticks", "-3").has_value());

  Settings result;
  fbs::settings::GetSettings(packed.GetBufferPointer())->UnPackTo(&result);
  EXPECT_EQ(result.core->fps, 30.5);
  EXPECT_EQ(result.net->max_late_ticks, 40u);
  EXPECT_EQ(result.net->clock_catch_up_threshold_ticks, -3);
}

TEST(SchemaCliTest, RejectsWhatCannotBeWritten) {
  Settings settings = MakeSettings();
  auto packed = Pack(settings);
  auto* table = flatbuffers::GetMutableRoot<flatbuffers::Table>(packed.GetBufferPointer());
  const reflection::Object& root = *Schema().root_table();

  EXPECT_FALSE(schema::SetFieldFromText(Schema(), root, *table, "core.fps", "fast").has_value());
  EXPECT_FALSE(schema::SetFieldFromText(Schema(), root, *table, "core.fps", "30x").has_value());
  EXPECT_FALSE(schema::SetFieldFromText(Schema(), root, *table, "net.max_late_ticks", "-1").has_value());
  EXPECT_FALSE(schema::SetFieldFromText(Schema(), root, *table, "net.max_actions_per_client", "5000000000").has_value())
      << "a uint32 field must not wrap";
  EXPECT_FALSE(schema::SetFieldFromText(Schema(), root, *table, "net.no_such_field", "1").has_value());
  EXPECT_FALSE(schema::SetFieldFromText(Schema(), root, *table, "core", "1").has_value());
}

TEST(SchemaCliTest, CommandLineValuesOverrideTheBase) {
  Settings base = MakeSettings();
  base.core->fps = 30.;
  base.net->max_late_ticks = 50;

  const auto settings = ApplyArguments({"-f", "45", "--max-late-ticks=70", "--connect-limit", "9"}, base);

  ASSERT_TRUE(settings.has_value()) << settings.error();
  EXPECT_EQ(settings->core->fps, 45.) << "the command line wins over the file";
  EXPECT_EQ(settings->net->max_late_ticks, 70u);
  EXPECT_EQ(settings->connect_timeout->limit, 9u);
  EXPECT_EQ(settings->net->send_interval_ticks, base.net->send_interval_ticks) << "untouched fields stay";
}

TEST(SchemaCliTest, NoArgumentsLeaveTheBaseAlone) {
  Settings base = MakeSettings();
  base.core->fps = 30.;

  const auto settings = ApplyArguments({}, base);

  ASSERT_TRUE(settings.has_value()) << settings.error();
  EXPECT_EQ(*settings, base);
}

TEST(SchemaCliTest, OverridesAreValidatedLikeFileValues) {
  const auto zero = ApplyArguments({"--fps", "0"}, MakeSettings());
  ASSERT_FALSE(zero.has_value());
  EXPECT_NE(zero.error().find("core.fps"), std::string::npos) << zero.error();

  EXPECT_FALSE(ApplyArguments({"--fps", "abc"}, MakeSettings()).has_value());
  EXPECT_FALSE(ApplyArguments({"--max-late-ticks", "1000"}, MakeSettings()).has_value())
      << "retention no longer covers the late window";
}

TEST(SchemaCliTest, NonFiniteCommandLineValuesAreRejected) {
  EXPECT_FALSE(ApplyArguments({"--fps", "nan"}, MakeSettings()).has_value());
  EXPECT_FALSE(ApplyArguments({"--clock-offset-smoothing", "nan"}, MakeSettings()).has_value());
}

TEST(SchemaCliTest, UnknownOptionIsRejectedByTheParser) {
  EXPECT_FALSE(ApplyArguments({"--frps", "30"}, MakeSettings()).has_value());
}

TEST(SchemaCliTest, OptionsShowUpInHelp) {
  Config config;
  ASSERT_TRUE(AddSettingsOptions(config).has_value());

  std::ostringstream help;
  help << config;

  EXPECT_NE(help.str().find("--fps"), std::string::npos);
  EXPECT_NE(help.str().find("-f"), std::string::npos);
  EXPECT_NE(help.str().find("--max-late-ticks"), std::string::npos);
  EXPECT_NE(help.str().find("--server"), std::string::npos) << "the built-in options stay";
}

TEST(SchemaCliTest, RegisteringTheSameSchemaTwiceIsANameClash) {
  Config config;
  ASSERT_TRUE(AddSettingsOptions(config).has_value());

  const auto again = AddSettingsOptions(config);

  ASSERT_FALSE(again.has_value());
  EXPECT_NE(again.error().find("already an option"), std::string::npos) << again.error();
}

TEST(SchemaCliTest, CoreTakesItsOptionsBeforeParsing) {
  std::string program = "z13_test";
  std::string option = "--fps=24";
  char* argv[] = {program.data(), option.data()};

  Core core(2, argv, [](Config& config) { return AddSettingsOptions(config); });

  EXPECT_FALSE(core.GetConfigError().has_value()) << *core.GetConfigError();
}

TEST(SchemaCliTest, CoreReportsAFailedRegistration) {
  std::string program = "z13_test";
  char* argv[] = {program.data()};

  Core core(1, argv, [](Config&) -> std::expected<void, std::string> { return std::unexpected("no options today"); });

  ASSERT_TRUE(core.GetConfigError().has_value());
  EXPECT_EQ(*core.GetConfigError(), "no options today");
}

}  // namespace
}  // namespace z13
