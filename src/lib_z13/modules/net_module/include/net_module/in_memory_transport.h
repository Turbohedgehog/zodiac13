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
#include <memory>
#include <string>

#include <net_module/transport.h>

namespace z13::net {

// Delay is sampled uniformly from [min_delay_ticks, max_delay_ticks] per packet, which
// gives reordering/jitter without a separate flag.
struct FaultConfig {
  // Unreliable traffic only -- see QueuePacket in in_memory_transport.cpp.
  double drop_probability {};
  uint32_t min_delay_ticks {};
  uint32_t max_delay_ticks {};
};

// Defined in in_memory_transport.cpp; only ever held behind a shared_ptr here.
class InMemoryNetworkState;

// A deterministic, seeded virtual network shared by however many InMemoryTransport
// instances a test creates -- no real sockets, no threads.
class InMemoryNetwork {
 public:
  explicit InMemoryNetwork(uint64_t seed = 1);

  void SetFaultConfig(FaultConfig config);

  // Advances virtual time by one tick, delivering whatever's now due. Call once per
  // test "frame", after every transport has queued its sends for this tick.
  void Tick();

  // For CreateInMemoryServerTransport/CreateInMemoryClientTransport and
  // InMemoryTransport: an opaque handle, since InMemoryNetworkState is only fully
  // defined in in_memory_transport.cpp -- no friend declaration needed.
  const std::shared_ptr<InMemoryNetworkState>& GetState() const { return state_; }

 private:
  std::shared_ptr<InMemoryNetworkState> state_;
};

// Binds `port` on `network`; error if another live transport already listens there.
std::expected<std::unique_ptr<Transport>, std::string> CreateInMemoryServerTransport(
    InMemoryNetwork& network, uint16_t port);

// Starts connecting to whichever transport listens on `server_port`, mirroring
// EnetTransport's async connect (a kConnected/kDisconnected event from Service()).
std::unique_ptr<Transport> CreateInMemoryClientTransport(InMemoryNetwork& network, uint16_t server_port);

}  // namespace z13::net
