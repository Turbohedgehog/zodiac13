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

#include <lib_core/config.h>
#include <lib_core/core_settings.h>

namespace z13 {
namespace {

TEST(ConfigTest, CoreSettingsAreReadThroughTheGetters) {
  Config config;
  CoreSettings settings;
  settings.fps = 30.;
  settings.snapshot_interval_seconds = 2.;
  settings.snapshot_retention_seconds = 8.;
  config.SetCoreSettings(settings);

  EXPECT_EQ(config.GetFPS(), 30.);
  EXPECT_EQ(config.GetSnapshotIntervalSeconds(), 2.);
  EXPECT_EQ(config.GetSnapshotRetentionSeconds(), 8.);
}

TEST(ConfigTest, FpsOverrideWinsUntilCleared) {
  Config config;
  const double configured = config.GetFPS();

  config.OverrideFps(24.);
  EXPECT_EQ(config.GetFPS(), 24.);
  EXPECT_EQ(config.GetCoreSettings().fps, configured) << "the configured value is untouched";

  config.OverrideFps(std::nullopt);
  EXPECT_EQ(config.GetFPS(), configured);
}

}  // namespace
}  // namespace z13
