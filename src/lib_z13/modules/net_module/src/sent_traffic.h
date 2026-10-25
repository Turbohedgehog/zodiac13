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
#include <optional>

#include <boost/container/flat_map.hpp>

#include <net_module/protocol.h>
#include <net_module/transport.h>

namespace z13::net {

// What a session has sent, counted where NetSession hands the packets to the transport.
struct SentTraffic {
  struct Link {
    ConnectionId connection {};
    Channel channel {};

    auto operator<=>(const Link&) const = default;
  };

  // `message` is the Envelope's body, empty for a packet of another format (the hints).
  void Record(const Link& link, std::optional<fbs::net::MessageBody> message, size_t bytes);
  TrafficStats Total() const;

  boost::container::flat_map<Link, TrafficStats> by_link;
  boost::container::flat_map<fbs::net::MessageBody, TrafficStats> by_message;
};

}  // namespace z13::net
