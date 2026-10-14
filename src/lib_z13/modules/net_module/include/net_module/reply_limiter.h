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
#include <unordered_map>

#include <lib_core/utils/endpoint.h>

namespace z13::net {

// A server answers one address at most once an interval, so discovery queries can't make it
// flood the network; addresses not heard from for longer are forgotten.
class ReplyLimiter {
 public:
  bool Allow(const Endpoint& from, uint64_t now_ms, uint64_t interval_ms);

 private:
  std::unordered_map<Endpoint, uint64_t, EndpointHash> replied_at_ms_;
};

}  // namespace z13::net
