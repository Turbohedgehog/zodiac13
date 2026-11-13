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

#include <lib_core/utils/file_pack_reader.h>

#include <format>
#include <system_error>

namespace z13 {

FilePackReader::FilePackReader(const std::filesystem::path& path)
    : path_(path), file_(path, std::ios::binary) {
  std::error_code error;
  const auto size = std::filesystem::file_size(path, error);
  size_ = error ? 0 : size;
}

std::expected<std::string, std::string> FilePackReader::Read(std::uint64_t offset, std::uint64_t size) const {
  const std::lock_guard lock(mutex_);
  if (!file_.is_open()) {
    return std::unexpected(std::format("cannot open '{}'", path_.string()));
  }
  std::string bytes(size, '\0');
  file_.clear();
  file_.seekg(static_cast<std::streamoff>(offset));
  if (!file_.read(bytes.data(), static_cast<std::streamsize>(size))) {
    return std::unexpected(std::format("cannot read {} bytes at {} from '{}'", size, offset, path_.string()));
  }
  return bytes;
}

}  // namespace z13
