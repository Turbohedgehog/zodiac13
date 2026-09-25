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

#include <net_module/net_module_factory.h>

#include <utility>

#include <flecs.h>

#include <boost/dll/alias.hpp>

#include <lib_core/flecs_utils.h>
#include <lib_core/world_state.h>

#include <net_module/enet_transport.h>

#include "net_module.h"
#include "transport_factories.h"

namespace z13::net {

ModuleFactoryPtr NetModuleFactory::CreateFactory() {
  return std::make_shared<NetModuleFactory>();
}

void NetModuleFactory::RegisterModules(flecs::world& world) {
  world.import<NetModule>();

  z13::flecs_tools::RegisterComponent<TransportFactories>(world);
  world.set<TransportFactories>({
      .server = server_transport_factory_
          ? server_transport_factory_
          : ServerTransportFactory([](uint16_t port) { return CreateEnetServerTransport(port); }),
      .client = client_transport_factory_
          ? client_transport_factory_
          : ClientTransportFactory([](const Endpoint& server, z13::ConnectTimeoutConfig timeout) {
              return CreateEnetClientTransport(server, timeout);
            }),
  });
}

const std::string& NetModuleFactory::GetName() const {
  static std::string name = "NetModuleFactory";

  return name;
}

void NetModuleFactory::SetTransportFactories(
    ServerTransportFactory server_factory, ClientTransportFactory client_factory) {
  server_transport_factory_ = std::move(server_factory);
  client_transport_factory_ = std::move(client_factory);
}

}  // namespace z13::net

extern "C" {

BOOST_DLL_ALIAS(
    z13::net::NetModuleFactory::CreateFactory,
    create_module_factory
)

}  // extern "C"
