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


#include "mcp_system.h"

#include <string>
#include <string_view>
#include <utility>

#include <lib_core/commands/command_registry.h>
#include <lib_core/state/world_state.h>
#include <lib_core/utils/flecs_utils.h>
#include <lib_core/utils/log.h>
#include <lib_core/world/lifecycle.h>
#include <z13/components/console/assistant_chat.h>
#include <z13_settings/settings.h>

#include "mcp_filter.h"
#include "mcp_protocol.h"

namespace z13::mcp {

namespace {

constexpr std::string_view kSayEvent = "say";

void RegisterComponents(flecs::world world) {
  flecs_tools::RegisterComponents<McpServerHandle, z13::McpServerSettings, console::AssistantChat>(world);
}

// What the player said to the assistant goes to the clients that follow the chat, as it is said.
void ServeChat(McpServer& server, console::AssistantChat& chat) {
  chat.listening = server.Subscribers() > 0;
  if (!chat.listening) {
    return;
  }
  for (const std::string& text : chat.unsent) {
    server.Publish(FormatEvent(kSayEvent, text));
  }
  chat.unsent.clear();
}

// Hands the requests that came in to the commands, which answer in this or the next frame's command phases.
void Pump(flecs::world world, const z13::McpServerSettings& settings, McpServerHandle& handle,
          console::AssistantChat* chat) {
  if (!settings.enabled) {
    handle = {};
    return;
  }
  if (!handle.server && handle.failed_port != settings.port) {
    auto started = McpServer::Start(static_cast<std::uint16_t>(settings.port));
    if (started) {
      handle.server = *started;
      log_info("MCP server listens on 127.0.0.1:{}", handle.server->Port());
    } else {
      handle.failed_port = settings.port;
      log_warn("MCP server did not start: {}", started.error());
    }
  }
  if (!handle.server) {
    return;
  }
  handle.server->NotePump();
  if (chat != nullptr) {
    ServeChat(*handle.server, *chat);
  }
  const CommandFilter filter = FilterOf(settings);
  for (McpCall& call : handle.server->TakeCalls()) {
    const auto reply = [server = handle.server, session = call.session, id = call.request.id](CommandResult result) {
      server->Reply(session, FormatReply(id, result));
    };
    Status submitted;
    {
      const ImmediateScope immediate(world);
      submitted = SubmitCommand(world, filter, call.request.command, call.request.arguments, reply);
    }
    if (!submitted) {
      reply(std::unexpected(submitted.error()));
    }
  }
}

void RegisterSystems(flecs::world world) {
  // The launcher overwrites the settings with the loaded ones (z13::InstallSettings).
  world.set<z13::McpServerSettings>({});
  world.set<McpServerHandle>({});
  world.system<const z13::McpServerSettings, McpServerHandle, console::AssistantChat*>("McpServerSystem::Pump")
      .kind(flecs::PreFrame)  // a frame phase: the simulation phases are off in the menu
      .immediate()
      .each([](flecs::iter& it, size_t, const z13::McpServerSettings& settings, McpServerHandle& handle,
               console::AssistantChat* chat) { Pump(it.world(), settings, handle, chat); });
}

}  // namespace

void McpServerSystem::Register(flecs::world& world) {
  OnRegisterComponents(world, RegisterComponents);
  OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::mcp
