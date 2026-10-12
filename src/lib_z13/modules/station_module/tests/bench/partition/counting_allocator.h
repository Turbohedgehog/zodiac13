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

#include <cstddef>
#include <memory>

namespace z13::testing::partition {

// std::allocator that counts the bytes it holds, shared by its copies and rebinds: what a
// container that doesn't report its own size takes.
template <typename T>
class CountingAllocator {
 public:
  using value_type = T;

  CountingAllocator() = default;
  template <typename U>
  explicit(false) CountingAllocator(const CountingAllocator<U>& other) : bytes_(other.Counter()) {}

  T* allocate(size_t count) {
    *bytes_ += count * sizeof(T);
    return std::allocator<T> {}.allocate(count);
  }
  void deallocate(T* pointer, size_t count) {
    *bytes_ -= count * sizeof(T);
    std::allocator<T> {}.deallocate(pointer, count);
  }

  size_t Bytes() const { return *bytes_; }
  const std::shared_ptr<size_t>& Counter() const { return bytes_; }

  template <typename U>
  bool operator==(const CountingAllocator<U>& other) const {
    return bytes_ == other.Counter();
  }

 private:
  std::shared_ptr<size_t> bytes_ = std::make_shared<size_t>();
};

}  // namespace z13::testing::partition
