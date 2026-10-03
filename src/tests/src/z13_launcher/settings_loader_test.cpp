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

#include <filesystem>
#include <fstream>
#include <string>

#include <lib_core/config.h>
#include <lib_core/core.h>
#include <z13_launcher/settings_loader.h>
#include <z13_settings/settings.h>

namespace z13 {
namespace {

constexpr std::string_view kTestDirectoryName = "z13_settings_loader_test";
constexpr std::string_view kTestFileName = "settings.json";

class SettingsFileTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Per test: ctest runs them as parallel processes.
    directory_ = std::filesystem::temp_directory_path() / kTestDirectoryName /
                 ::testing::UnitTest::GetInstance()->current_test_info()->name();
    std::filesystem::remove_all(directory_);
  }

  void TearDown() override { std::filesystem::remove_all(directory_); }

  std::filesystem::path Path() const { return directory_ / kTestFileName; }

  std::filesystem::path directory_;
};

TEST(SettingsLoaderTest, EmptyObjectGivesTheSchemaDefaults) {
  const auto settings = ParseSettings("{}");

  ASSERT_TRUE(settings.has_value()) << settings.error();
  EXPECT_EQ(*settings, MakeSettings());
}

TEST(SettingsLoaderTest, GivenKeysOverrideAndTheRestKeepTheirDefaults) {
  const auto settings = ParseSettings(
      R"({"core": {"fps": 30}, "connect_timeout": {"limit": 8}, "net": {"max_late_ticks": 40, "clock_offset_smoothing": 0.5}})");

  ASSERT_TRUE(settings.has_value()) << settings.error();
  Settings expected = MakeSettings();
  expected.core->fps = 30.;
  expected.connect_timeout->limit = 8;
  expected.net->max_late_ticks = 40;
  expected.net->clock_offset_smoothing = 0.5;
  EXPECT_EQ(*settings, expected);
}

TEST(SettingsLoaderTest, UnknownKeyIsRejected) {
  EXPECT_FALSE(ParseSettings(R"({"fpz": 30})").has_value());
  EXPECT_FALSE(ParseSettings(R"({"core": {"fpz": 30}})").has_value());
  EXPECT_FALSE(ParseSettings(R"({"net": {"max_late_tick": 40}})").has_value());
}

TEST(SettingsLoaderTest, MalformedJsonIsRejected) {
  EXPECT_FALSE(ParseSettings("{ core: ").has_value());
}

TEST(SettingsLoaderTest, ValuesThatWouldDivideByZeroAreRejected) {
  EXPECT_FALSE(ParseSettings(R"({"core": {"fps": 0}})").has_value());
  EXPECT_FALSE(ParseSettings(R"({"core": {"snapshot_interval_seconds": 0}})").has_value());
  EXPECT_FALSE(ParseSettings(R"({"core": {"max_tick_backlog": 0}})").has_value());
  EXPECT_FALSE(ParseSettings(R"({"net": {"send_interval_ticks": 0}})").has_value());
  EXPECT_FALSE(ParseSettings(R"({"net": {"clock_catch_up_every_ticks": 0}})").has_value());
  EXPECT_FALSE(ParseSettings(R"({"net": {"clock_offset_smoothing": 0.0}})").has_value());
  EXPECT_FALSE(ParseSettings(R"({"net": {"clock_offset_smoothing": 1.5}})").has_value());
}

TEST(SettingsLoaderTest, JumpThresholdBelowCatchUpThresholdIsRejected) {
  EXPECT_FALSE(
      ParseSettings(R"({"net": {"clock_catch_up_threshold_ticks": 5, "clock_jump_threshold_ticks": 4}})").has_value());
}

TEST(SettingsLoaderTest, InvertedConnectTimeoutIsRejected) {
  EXPECT_FALSE(ParseSettings(R"({"connect_timeout": {"min_timeout_ms": 5000, "max_timeout_ms": 1000}})").has_value());
}

TEST(SettingsLoaderTest, RetentionShorterThanTheLateWindowIsRejected) {
  // 96 late ticks at 60 fps plus a 1s interval need 2.6s of history.
  EXPECT_FALSE(ParseSettings(R"({"core": {"snapshot_retention_seconds": 2}})").has_value());
  EXPECT_TRUE(ParseSettings(R"({"core": {"snapshot_retention_seconds": 3}})").has_value());
  EXPECT_FALSE(ParseSettings(R"({"core": {"fps": 20}})").has_value()) << "96 ticks last 4.8s at 20 fps";
}

TEST(SettingsLoaderTest, SerializedSettingsParseBackToTheSame) {
  Settings settings = MakeSettings();
  settings.core->fps = 30.;
  settings.net->max_late_ticks = 40;
  settings.connect_timeout->limit = 8;

  const auto json = SerializeSettings(settings);
  ASSERT_TRUE(json.has_value()) << json.error();
  const auto parsed = ParseSettings(*json);

  ASSERT_TRUE(parsed.has_value()) << parsed.error();
  EXPECT_EQ(*parsed, settings);
  EXPECT_NE(json->find("max_late_ticks"), std::string::npos) << "defaults are written out too";
}

TEST_F(SettingsFileTest, MissingFileIsCreatedFromTheSchemaDefaults) {
  ASSERT_FALSE(std::filesystem::exists(Path()));

  const auto settings = ReadSettings(Path());

  ASSERT_TRUE(settings.has_value()) << settings.error();
  EXPECT_EQ(*settings, MakeSettings());
  ASSERT_TRUE(std::filesystem::exists(Path())) << "the data directory is created too";
  const auto reread = ReadSettings(Path());
  ASSERT_TRUE(reread.has_value()) << reread.error();
  EXPECT_EQ(*reread, MakeSettings());
}

TEST_F(SettingsFileTest, ReadsAnExistingFileWithoutOverwritingIt) {
  std::filesystem::create_directories(directory_);
  std::ofstream(Path()) << R"({"net": {"send_interval_ticks": 6}})";

  const auto settings = ReadSettings(Path());

  ASSERT_TRUE(settings.has_value()) << settings.error();
  EXPECT_EQ(settings->net->send_interval_ticks, 6u);
  std::ifstream file(Path());
  const std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  EXPECT_EQ(contents, R"({"net": {"send_interval_ticks": 6}})");
}

TEST_F(SettingsFileTest, AnInvalidFileIsAnErrorNotARegeneration) {
  std::filesystem::create_directories(directory_);
  std::ofstream(Path()) << R"({"core": {"fps": 0}})";

  EXPECT_FALSE(ReadSettings(Path()).has_value());
}

TEST(SettingsLoaderTest, InstalledSettingsReachConfigAndWorld) {
  char program[] = "z13_test";
  char* argv[] = {program};
  Core core(1, argv);
  Settings settings = MakeSettings();
  settings.core->fps = 30.;
  settings.core->snapshot_interval_seconds = 2.;
  settings.core->max_deferred_rollbacks = 3;
  settings.net->max_late_ticks = 7;
  core.GetConfig().SetCoreSettings(*settings.core);

  const auto created = core.CreateWorld();
  ASSERT_TRUE(created.has_value()) << created.error();
  flecs::world& world = created->get();
  InstallSettings(world, settings);

  EXPECT_EQ(core.GetConfig().GetFPS(), 30.);
  EXPECT_EQ(core.GetConfig().GetSnapshotIntervalSeconds(), 2.);
  EXPECT_EQ(world.get<ActiveCoreSettings>(), *settings.core);
  EXPECT_EQ(world.get<NetTuning>(), *settings.net);
}

}  // namespace
}  // namespace z13
