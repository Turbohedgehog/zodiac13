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

#include <net_module/reply_limiter.h>

namespace z13::net {

bool ReplyLimiter::Allow(const Endpoint& from, uint64_t now_ms, uint64_t interval_ms) {
  std::erase_if(replied_at_ms_, [&](const auto& replied) { return now_ms - replied.second >= interval_ms; });
  return replied_at_ms_.try_emplace(from, now_ms).second;
}

}  // namespace z13::net
