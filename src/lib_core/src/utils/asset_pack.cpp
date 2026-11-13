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

#include <lib_core/utils/asset_pack.h>

#include <cstring>
#include <optional>
#include <format>
#include <string_view>
#include <utility>

namespace z13 {

namespace {

constexpr std::string_view kMagic = "Z13PACK\x01";
constexpr std::size_t kHeaderSize = kMagic.size() + sizeof(std::uint64_t);
constexpr std::size_t kPathLengthSize = sizeof(std::uint16_t);
// Cuts a corrupt header's index size short before it allocates.
constexpr std::uint64_t kMaxIndexSize = 64ULL << 20;

template <typename T>
std::optional<T> Take(std::string_view& bytes) {
  if (bytes.size() < sizeof(T)) {
    return std::nullopt;
  }
  T value {};
  std::memcpy(&value, bytes.data(), sizeof(T));  // little-endian hosts only, as every target is
  bytes.remove_prefix(sizeof(T));
  return value;
}

std::optional<std::pair<std::string, PackEntry>> TakeEntry(std::string_view& bytes) {
  const auto length = Take<std::uint16_t>(bytes);
  if (!length || bytes.size() < *length) {
    return std::nullopt;
  }
  std::string path(bytes.substr(0, *length));
  bytes.remove_prefix(*length);
  const auto offset = Take<std::uint64_t>(bytes);
  const auto size = offset ? Take<std::uint64_t>(bytes) : std::nullopt;
  if (!size) {
    return std::nullopt;
  }
  return std::pair {std::move(path), PackEntry {.offset = *offset, .size = *size}};
}

}  // namespace

AssetPack::AssetPack(std::unique_ptr<PackReader> reader, std::map<std::string, PackEntry> index)
    : reader_(std::move(reader)), index_(std::move(index)) {}

std::expected<AssetPack, std::string> AssetPack::Open(std::unique_ptr<PackReader> reader) {
  const auto header = reader->Read(0, kHeaderSize);
  if (!header) {
    return std::unexpected(std::format("cannot read the pack's header: {}", header.error()));
  }
  std::string_view header_bytes = *header;
  if (!header_bytes.starts_with(kMagic)) {
    return std::unexpected("not an asset pack (wrong magic)");
  }
  header_bytes.remove_prefix(kMagic.size());
  const std::uint64_t index_size = Take<std::uint64_t>(header_bytes).value_or(0);
  if (index_size > kMaxIndexSize) {
    return std::unexpected(std::format("the pack's index claims {} bytes", index_size));
  }
  const auto index_bytes = reader->Read(kHeaderSize, index_size);
  if (!index_bytes) {
    return std::unexpected(std::format("cannot read the pack's index: {}", index_bytes.error()));
  }
  const std::uint64_t pack_size = reader->Size();
  const std::uint64_t data_start = kHeaderSize + index_size;
  std::string_view rest = *index_bytes;
  std::map<std::string, PackEntry> index;
  while (!rest.empty()) {
    auto entry = TakeEntry(rest);
    if (!entry) {
      return std::unexpected("the pack's index is cut short");
    }
    const PackEntry& placed = entry->second;
    if (placed.offset < data_start || placed.offset > pack_size || placed.size > pack_size - placed.offset) {
      return std::unexpected(std::format("'{}' lies outside the pack", entry->first));
    }
    index.insert(std::move(*entry));
  }
  return AssetPack(std::move(reader), std::move(index));
}

std::expected<std::string, std::string> AssetPack::Read(const std::string& path) const {
  const auto found = index_.find(path);
  if (found == index_.end()) {
    return std::unexpected(std::format("'{}' is not in the asset pack", path));
  }
  return reader_->Read(found->second.offset, found->second.size);
}

std::vector<std::string> AssetPack::List(const std::string& directory) const {
  const std::string prefix = directory.empty() || directory.ends_with('/') ? directory : directory + '/';
  std::vector<std::string> names;
  for (auto it = index_.lower_bound(prefix); it != index_.end() && it->first.starts_with(prefix); ++it) {
    const std::string_view name = std::string_view(it->first).substr(prefix.size());
    if (!name.contains('/')) {
      names.emplace_back(name);
    }
  }
  return names;
}

}  // namespace z13
