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

#include "apk_pack_reader.h"

#include <algorithm>
#include <format>

namespace z13 {

ApkPackReader::ApkPackReader(const std::string& path) : path_(path), stream_(SDL_IOFromFile(path.c_str(), "rb")) {
  if (stream_ != nullptr) {
    // A negative size (unknown) leaves 0, so no entry validates.
    size_ = static_cast<std::uint64_t>(std::max<Sint64>(SDL_GetIOSize(stream_), 0));
  }
}

ApkPackReader::~ApkPackReader() {
  if (stream_ != nullptr) {
    SDL_CloseIO(stream_);
  }
}

std::expected<std::string, std::string> ApkPackReader::Read(std::uint64_t offset, std::uint64_t size) const {
  const std::lock_guard lock(mutex_);
  if (stream_ == nullptr) {
    return std::unexpected(std::format("cannot open '{}' in the APK", path_));
  }
  std::string bytes(size, '\0');
  if (SDL_SeekIO(stream_, static_cast<Sint64>(offset), SDL_IO_SEEK_SET) < 0 ||
      SDL_ReadIO(stream_, bytes.data(), size) != size) {
    return std::unexpected(std::format("cannot read {} bytes at {} from '{}': {}", size, offset, path_, SDL_GetError()));
  }
  return bytes;
}

}  // namespace z13
