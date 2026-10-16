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
#include <vector>

#include <lib_core/utils/endpoint.h>
#include <net_module/discovery_messages.h>
#include <z13/components/net.h>

namespace z13::net {

struct HeardServer {
  // Its endpoint is chosen from `addresses` when listed.
  FoundServer server;
  // Each address it answered from, at its game port, with when it was last heard there.
  std::unordered_map<Endpoint, uint64_t, EndpointHash> addresses;
};

// The servers that answered a Join window's queries, one per server (ServerInfo::instance_id)
// however many of this machine's networks reach it.
class ServerFinder {
 public:
  // `from` is where the answer came from; `now_ms` on the clock the query was sent by.
  void Record(const Endpoint& from, const ServerInfo& info, uint64_t now_ms);
  void Forget(uint64_t now_ms, uint64_t after_ms);
  // By name, then address; a server is joined at its loopback address if it has one, else at
  // its lowest, so the row doesn't jump between addresses.
  std::vector<FoundServer> Servers() const;

 private:
  std::unordered_map<uint64_t, HeardServer> heard_;
};

}  // namespace z13::net
