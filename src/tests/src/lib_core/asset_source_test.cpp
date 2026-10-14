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
#include <string>
#include <vector>

#include <lib_core/utils/asset_source.h>
#include <lib_core/utils/file_io.h>

namespace z13 {
namespace {

constexpr std::string_view kRootName = "z13_asset_source_test";

// A root with assets/ (two files in a subdirectory, listed out of order) and config/.
class AssetSourceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Per test: ctest runs them as parallel processes.
    root_ = std::filesystem::temp_directory_path() / kRootName /
            ::testing::UnitTest::GetInstance()->current_test_info()->name();
    std::filesystem::remove_all(root_);
    ASSERT_TRUE(WriteFile(root_ / "assets" / "dir" / "b.json", "{}").has_value());
    ASSERT_TRUE(WriteFile(root_ / "assets" / "dir" / "a.bin", std::string("\0\1\2", 3)).has_value());
    ASSERT_TRUE(WriteFile(root_ / "config" / "list.yaml", "modules: []").has_value());
  }
  void TearDown() override { std::filesystem::remove_all(root_); }

  std::filesystem::path root_;
};

TEST_F(AssetSourceTest, ReadsAssetsAndConfigByteForByte) {
  const AssetSource source(root_);
  EXPECT_EQ(source.ReadAsset("dir/a.bin"), std::string("\0\1\2", 3));
  EXPECT_EQ(source.ReadConfig("list.yaml"), "modules: []");
}

TEST_F(AssetSourceTest, AMissingFileIsAnErrorNamingIt) {
  const auto read = AssetSource(root_).ReadAsset("dir/missing.json");
  ASSERT_FALSE(read.has_value());
  EXPECT_NE(read.error().find("missing.json"), std::string::npos) << read.error();
}

TEST_F(AssetSourceTest, ListsADirectorySorted) {
  const AssetSource source(root_);
  EXPECT_EQ(source.ListAssets("dir"), (std::vector<std::string> {"a.bin", "b.json"}));
  EXPECT_TRUE(source.ListAssets("missing").empty());
}

}  // namespace
}  // namespace z13
