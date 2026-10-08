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


#include "group_bucket.h"

#include <lib_core/state/snapshot_grouping.h>

namespace z13::flecs_tools {

void GroupBucket::Add(flecs::entity e, uint64_t hash) {
  sum_ += MixHash(hash);
  members_.push_back(e);
}

uint64_t GroupBucket::Fingerprint() const {
  return MixHash(sum_ ^ MixHash(members_.size()));
}

}  // namespace z13::flecs_tools
