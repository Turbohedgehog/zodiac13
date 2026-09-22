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

#include <lib_core/endpoint.h>

#include <charconv>
#include <format>

namespace z13 {

namespace {

std::expected<uint16_t, std::string> ParsePort(std::string_view text) {
  int port {};
  const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), port);
  if (ec != std::errc{} || ptr != text.data() + text.size()) {
    return std::unexpected(std::format("invalid port '{}'", text));
  }
  if (port < kMinPort || port > kMaxPort) {
    return std::unexpected(std::format("port {} out of range [{}, {}]", port, kMinPort, kMaxPort));
  }
  return static_cast<uint16_t>(port);
}

}  // namespace

std::expected<Endpoint, std::string> ParseEndpoint(std::string_view text, uint16_t default_port) {
  if (text.empty()) {
    return std::unexpected("endpoint is empty");
  }

  const auto colon = text.rfind(':');
  if (colon == std::string_view::npos) {
    return Endpoint{.host = std::string(text), .port = default_port};
  }

  const std::string_view host = text.substr(0, colon);
  const std::string_view port_text = text.substr(colon + 1);
  if (host.empty()) {
    return std::unexpected("endpoint has no host");
  }
  if (port_text.empty()) {
    return std::unexpected("endpoint has no port after ':'");
  }

  const auto port = ParsePort(port_text);
  if (!port) {
    return std::unexpected(port.error());
  }
  return Endpoint{.host = std::string(host), .port = *port};
}

}  // namespace z13
