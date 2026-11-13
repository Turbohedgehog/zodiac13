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

#include <cstdint>
#include <expected>
#include <string>

namespace z13 {

// Random access to the bytes of an archive wherever it lies (a file, the Android APK).
// Safe to call from several threads.
class PackReader {
 public:
  virtual ~PackReader() = default;

  virtual std::uint64_t Size() const = 0;
  virtual std::expected<std::string, std::string> Read(std::uint64_t offset, std::uint64_t size) const = 0;
};

}  // namespace z13
