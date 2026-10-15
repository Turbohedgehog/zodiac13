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

#include <expected>
#include <memory>
#include <string>

namespace z13::net {

// enet_initialize()/enet_deinitialize() are process-global, not per host or socket, but
// transports and discovery sockets come and go independently: each holds a share, the last
// one out deinitializes.
class EnetLifetime {
 public:
  ~EnetLifetime();
};

std::expected<std::shared_ptr<EnetLifetime>, std::string> AcquireEnetLifetime();

}  // namespace z13::net
