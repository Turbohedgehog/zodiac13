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

#pragma once

#include <filesystem>
#include <fstream>
#include <mutex>

#include <lib_core/utils/pack_reader.h>

namespace z13 {

class FilePackReader : public PackReader {
 public:
  // The file is opened here; a missing one makes every Read fail.
  explicit FilePackReader(const std::filesystem::path& path);

  std::uint64_t Size() const override { return size_; }
  std::expected<std::string, std::string> Read(std::uint64_t offset, std::uint64_t size) const override;

 private:
  std::filesystem::path path_;
  std::uint64_t size_ {};
  mutable std::mutex mutex_;
  mutable std::ifstream file_;
};

}  // namespace z13
