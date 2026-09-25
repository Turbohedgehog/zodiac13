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
#include <functional>
#include <memory>
#include <string>

#include <boost/config.hpp>

#include <lib_core/config.h>
#include <lib_core/endpoint.h>
#include <lib_core/module_factory_base.h>

#include <net_module/transport.h>

// The BOOST_DLL_ALIAS export lives in net_module_factory.cpp, not here -- see
// bullet_module_factory.h for why (same-name symbol clash when test code links
// multiple plugin factories into one binary).

namespace z13::net {

using ServerTransportFactory =
    std::function<std::expected<std::unique_ptr<Transport>, std::string>(uint16_t port)>;
using ClientTransportFactory = std::function<std::expected<std::unique_ptr<Transport>, std::string>(
    const Endpoint& server, z13::ConnectTimeoutConfig connect_timeout)>;

class BOOST_SYMBOL_VISIBLE NetModuleFactory : public z13::ModuleFactoryBase {
 public:
  static ModuleFactoryPtr CreateFactory();

  void RegisterModules(flecs::world& world) override;
  const std::string& GetName() const override;

  // Defaults to real ENet transports when left unset; tests substitute InMemoryTransport
  // factories so multiple Z13TestWorlds can talk to each other deterministically.
  void SetTransportFactories(ServerTransportFactory server_factory, ClientTransportFactory client_factory);

 private:
  ServerTransportFactory server_transport_factory_;
  ClientTransportFactory client_transport_factory_;
};

}  // namespace z13::net
