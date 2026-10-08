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
#include <vector>

#include <flecs.h>

namespace z13::flecs_tools {

// The members a scan found in one snapshot group, with their fingerprint.
class GroupBucket {
 public:
  void Add(flecs::entity e, uint64_t hash);
  uint64_t Fingerprint() const;
  const std::vector<flecs::entity>& Members() const { return members_; }

 private:
  uint64_t sum_ {};  // a sum, so the order members are found in doesn't matter
  std::vector<flecs::entity> members_;
};

}  // namespace z13::flecs_tools
