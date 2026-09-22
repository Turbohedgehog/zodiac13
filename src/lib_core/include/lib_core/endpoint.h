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
#include <string>
#include <string_view>

namespace z13 {

inline constexpr int kMinPort = 1;
inline constexpr int kMaxPort = 65535;

struct Endpoint {
  std::string host;
  uint16_t port {};

  friend bool operator==(const Endpoint&, const Endpoint&) = default;
};

// Parses "host" or "host:port" (bare "host" uses `default_port`). Shared by --connect
// and the future Join dialog, so both validate the same way. No IPv6 literals yet --
// their embedded ':' would be ambiguous with the port separator.
std::expected<Endpoint, std::string> ParseEndpoint(std::string_view text, uint16_t default_port);

}  // namespace z13
