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
#include <span>

#include <enet/enet.h>

#include <lib_core/utils/endpoint.h>
#include <lib_core/utils/status.h>

namespace z13::net {

// One UDP datagram outside ENet's protocol, through an ENet socket (a host's or a discovery socket).
Status SendDatagram(ENetSocket socket, const Endpoint& to, std::span<const uint8_t> bytes);

Endpoint EndpointOf(const ENetAddress& address);

}  // namespace z13::net
