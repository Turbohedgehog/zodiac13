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
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <lib_core/utils/asset_pack.h>
#include <lib_core/utils/asset_source.h>
#include <lib_core/utils/file_io.h>
#include <lib_core/utils/file_pack_reader.h>
#include <lib_core/utils/pack_reader.h>

namespace z13 {
namespace {

constexpr std::string_view kMagic = "Z13PACK\x01";

template <typename T>
void Append(std::string& bytes, T value) {
  bytes.append(reinterpret_cast<const char*>(&value), sizeof(T));
}

// The layout tools/pack_assets.py writes, by hand: the script is the only writer, so the test
// of the shipped archive below is what keeps the two in step.
std::string MakePack(const std::map<std::string, std::string>& files) {
  std::string index;
  std::string data;
  const std::size_t index_size = std::ranges::fold_left(
      files, std::size_t {}, [](std::size_t sum, const auto& file) { return sum + sizeof(std::uint16_t) + file.first.size() + 2 * sizeof(std::uint64_t); });
  const std::size_t data_start = kMagic.size() + sizeof(std::uint64_t) + index_size;
  for (const auto& [path, contents] : files) {
    Append(index, static_cast<std::uint16_t>(path.size()));
    index += path;
    Append(index, static_cast<std::uint64_t>(data_start + data.size()));
    Append(index, static_cast<std::uint64_t>(contents.size()));
    data += contents;
  }
  std::string pack(kMagic);
  Append(pack, static_cast<std::uint64_t>(index.size()));
  return pack + index + data;
}

class MemoryReader : public PackReader {
 public:
  explicit MemoryReader(std::string bytes) : bytes_(std::move(bytes)) {}
  std::uint64_t Size() const override { return bytes_.size(); }
  std::expected<std::string, std::string> Read(std::uint64_t offset, std::uint64_t size) const override {
    if (offset + size > bytes_.size()) {
      return std::unexpected("past the end");
    }
    return bytes_.substr(offset, size);
  }

 private:
  std::string bytes_;
};

std::expected<AssetPack, std::string> OpenBytes(std::string bytes) {
  return AssetPack::Open(std::make_unique<MemoryReader>(std::move(bytes)));
}

const std::map<std::string, std::string> kFiles = {
    {"assets/dir/b.json", "{}"},
    {"assets/dir/a.bin", std::string("\0\1\2", 3)},
    {"assets/dir/sub/c.txt", "deep"},
    {"config/list.yaml", "modules: []"},
};

TEST(AssetPackTest, ReadsEveryFileByteForByte) {
  const auto pack = OpenBytes(MakePack(kFiles));
  ASSERT_TRUE(pack.has_value()) << pack.error();
  for (const auto& [path, contents] : kFiles) {
    EXPECT_EQ(pack->Read(path), contents) << path;
  }
}

TEST(AssetPackTest, AMissingFileIsAnErrorNamingIt) {
  const auto pack = OpenBytes(MakePack(kFiles));
  ASSERT_TRUE(pack.has_value());
  const auto read = pack->Read("assets/dir/missing.json");
  ASSERT_FALSE(read.has_value());
  EXPECT_NE(read.error().find("missing.json"), std::string::npos) << read.error();
}

TEST(AssetPackTest, ListsTheFilesDirectlyInADirectorySorted) {
  const auto pack = OpenBytes(MakePack(kFiles));
  ASSERT_TRUE(pack.has_value());
  EXPECT_EQ(pack->List("assets/dir"), (std::vector<std::string> {"a.bin", "b.json"}));
  EXPECT_TRUE(pack->List("assets/none").empty());
}

TEST(AssetPackTest, RejectsWrongMagicAndATruncatedIndex) {
  std::string wrong = MakePack(kFiles);
  wrong[0] = 'X';
  EXPECT_FALSE(OpenBytes(wrong).has_value());

  const std::string whole = MakePack(kFiles);
  EXPECT_FALSE(OpenBytes(whole.substr(0, kMagic.size() + sizeof(std::uint64_t) + 5)).has_value());
  EXPECT_FALSE(OpenBytes("").has_value());
}

TEST(AssetPackTest, RejectsAnEntryThatLiesPastTheEndOfThePack) {
  const std::string whole = MakePack(kFiles);
  const auto cut = OpenBytes(whole.substr(0, whole.size() - 1));
  ASSERT_FALSE(cut.has_value());
  EXPECT_NE(cut.error().find("outside"), std::string::npos) << cut.error();
}

TEST(AssetPackTest, ASourceOverAPackReadsAssetsAndConfig) {
  auto pack = OpenBytes(MakePack(kFiles));
  ASSERT_TRUE(pack.has_value());
  const AssetSource source(std::make_shared<const AssetPack>(*std::move(pack)));
  EXPECT_EQ(source.ReadAsset("dir/sub/c.txt"), "deep");
  EXPECT_EQ(source.ReadConfig("list.yaml"), "modules: []");
  EXPECT_EQ(source.ListAssets("dir"), (std::vector<std::string> {"a.bin", "b.json"}));
  EXPECT_FALSE(source.ReadAsset("dir/none").has_value());
}

// The build's z13.pack (the z13_asset_pack target) holds what the source tree's directories do.
TEST(AssetPackTest, TheShippedPackHoldsEveryFileOfAssetsAndConfig) {
  const std::filesystem::path root = std::filesystem::path(Z13_SOURCE_ASSETS_DIR).parent_path();
  auto pack = AssetPack::Open(std::make_unique<FilePackReader>(Z13_ASSET_PACK));
  ASSERT_TRUE(pack.has_value()) << pack.error();
  std::size_t count = 0;
  for (const std::string_view directory : {"assets", "config"}) {
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root / directory)) {
      if (!entry.is_regular_file()) {
        continue;
      }
      ++count;
      const std::string path = std::filesystem::relative(entry.path(), root).generic_string();
      const auto packed = pack->Read(path);
      const auto original = ReadFile(entry.path());
      ASSERT_TRUE(packed.has_value()) << path << ": " << packed.error();
      ASSERT_TRUE(original.has_value());
      EXPECT_TRUE(*packed == *original) << path;
    }
  }
  EXPECT_EQ(pack->Count(), count) << "the pack holds files the tree doesn't";
}

}  // namespace
}  // namespace z13
