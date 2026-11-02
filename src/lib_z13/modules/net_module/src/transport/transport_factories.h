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

#include <net_module/net_module_factory.h>

namespace z13::net {

// Runtime singleton wrapping the two factories NetModuleFactory was configured with
// (real ENet by default). Not state -- never saved/restored.
struct TransportFactories {
  using Singleton = void;

  ServerTransportFactory server;
  ClientTransportFactory client;
};

}  // namespace z13::net
