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

#include "sent_traffic.h"

#include <numeric>

namespace z13::net {

namespace {

void Add(TrafficStats& stats, size_t bytes) {
  ++stats.packets;
  stats.bytes += bytes;
}

}  // namespace

void SentTraffic::Record(const Link& link, std::optional<fbs::net::MessageBody> message, size_t bytes) {
  Add(by_link[link], bytes);
  if (message) {
    Add(by_message[*message], bytes);
  }
}

TrafficStats SentTraffic::Total() const {
  return std::accumulate(by_link.begin(), by_link.end(), TrafficStats {}, [](TrafficStats total, const auto& entry) {
    total.packets += entry.second.packets;
    total.bytes += entry.second.bytes;
    return total;
  });
}

}  // namespace z13::net
