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

#include "enet_lifetime.h"

#include <enet/enet.h>

namespace z13::net {

EnetLifetime::~EnetLifetime() {
  enet_deinitialize();
}

// The weak_ptr is the one justified exception to "no static/globals" (CLAUDE.md): ENet's own
// init call has no other way to know "am I the last host".
std::expected<std::shared_ptr<EnetLifetime>, std::string> AcquireEnetLifetime() {
  static std::weak_ptr<EnetLifetime> weak_instance;
  if (auto existing = weak_instance.lock()) {
    return existing;
  }
  if (enet_initialize() != 0) {
    return std::unexpected("enet_initialize failed");
  }
  auto instance = std::make_shared<EnetLifetime>();
  weak_instance = instance;
  return instance;
}

}  // namespace z13::net
