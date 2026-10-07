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

#include "net_session_system.h"

#include <cstdint>
#include <cstdlib>
#include <utility>

#include <flecs.h>

#include <lib_core/state/rollback.h>
#include <lib_core/state/world_serializer.h>
#include <lib_core/state/world_snapshot_history.h>
#include <lib_core/state/world_state.h>
#include <lib_core/time/simulation_clock.h>
#include <lib_core/utils/flecs_utils.h>
#include <lib_core/utils/log.h>
#include <lib_core/world/components.h>
#include <lib_core/world/lifecycle.h>

#include <z13/components/gameplay.h>
#include <z13/components/net.h>
#include <z13_settings/settings.h>

#include <net_module/clock_sync.h>
#include <net_module/state_digest.h>

#include "client_session.h"
#include "net_session.h"
#include "server_session.h"
#include "session_control.h"
#include "session_deltas.h"
#include "session_settings.h"
#include "state_digest_system.h"
#include "transport_factories.h"

namespace z13::net {

namespace {

namespace ft = z13::flecs_tools;

// OnSet, not OnAdd: see CLAUDE.md.
void OnStartServerRequest(flecs::entity e, const StartServerRequest& request, const TransportFactories& factories) {
  flecs::world world = e.world();
  const uint16_t port = request.port;
  e.destruct();
  if (world.has<NetSession>()) {
    log_warn("NetSession: ignoring a StartServerRequest, a session is already open");
    return;
  }

  auto transport = factories.server(port);
  if (!transport) {
    log_critical("NetSession: failed to open the server on port {}: {}", port, transport.error());
    SetConnectionStatus(world, ConnectionState::kFailed, transport.error());
    // A dedicated server has no menu to fall back to.
    if (const auto config = z13::GetCoreConfig(world); config && config->get().IsServer()) {
      z13::ShutdownCore(world, EXIT_FAILURE);
    }
    return;
  }

  ft::ResetSessionScopedComponents(world);
  world.set<ft::SnapshotCaptureRate>({.per_interval = world.get<NetTuning>().rollback_snapshots_per_interval});
  NetSession session;
  session.Open(std::move(*transport));
  world.set<NetSession>(std::move(session));
  world.add<ServerRole>();
  SetConnectionStatus(world, ConnectionState::kConnected);
  world.remove<z13::gameplay::Pause>();
  world.add<z13::gameplay::Gameplay>();
}

void OnJoinRequest(flecs::entity e, const JoinRequest& request, const TransportFactories& factories) {
  flecs::world world = e.world();
  if (world.has<NetSession>()) {
    log_warn("NetSession: ignoring a JoinRequest, a session is already open");
    e.destruct();
    return;
  }
  ft::ResetSessionScopedComponents(world);
  world.set<ft::SnapshotCaptureRate>({.per_interval = world.get<NetTuning>().rollback_snapshots_per_interval});
  world.add<ClientRole>();
  SetConnectionStatus(world, ConnectionState::kConnecting);

  const z13::ConnectTimeoutConfig timeout = world.get<z13::ConnectTimeout>();
  auto transport = factories.client(request.endpoint, timeout);
  if (!transport) {
    SetConnectionStatus(world, ConnectionState::kFailed, transport.error());
    world.remove<ClientRole>();
    e.destruct();
    return;
  }

  NetSession session;
  session.Open(std::move(*transport));
  world.set<NetSession>(std::move(session));
  e.destruct();
}

void OnLeaveRequest(flecs::entity e) {
  flecs::world world = e.world();
  e.destruct();
  EndSession(world);
  SetConnectionStatus(world, ConnectionState::kNone);
}

void ServiceServer(flecs::world world, NetSession& session) {
  if (world.has<ft::RollbackFailed>()) {
    log_error("NetSession(server): {}", world.get<ft::RollbackFailed>().reason);
    world.remove<ft::RollbackFailed>();
  }
  // Only the capture a Welcome or Resync needs fails here; the server can't go on without it.
  if (const Status serviced = ServiceServerSession(world, session, world.get_mut<z13::gameplay::IdCounters>());
      !serviced) {
    log_error("NetSession(server): {}", serviced.error());
    EndSession(world);
    return;
  }
  SendSettledStateDigests(world, session, world.get_mut<StateDigests>());
}

void ServiceClient(flecs::world world, NetSession& session) {
  // Before servicing: digests received last frame are checked once the rollbacks their
  // preceding commands caused have landed.
  ResyncIfDiverged(world, session);
  if (!FinishPendingJoin(world)) {
    return;
  }
  ServiceClientSession(world, session);
  if (world.has<NetSession>()) {
    SendPingIfDue(world, session);
  }
}

void ServiceNetSession(flecs::world world) {
  // Suspends defer: .immediate() alone still defers this SaveState()'s writes.
  const z13::ImmediateScope immediate(world);

  ApplyDueSessionDeltas(world);

  if (!world.has<NetSession>()) {
    return;
  }

  // Exit to Main Menu only removes Gameplay; the session goes with it. A set local id
  // means Gameplay was already there (a client may still be catching up after Welcome).
  if (world.get<z13::gameplay::LocalPlayer>().id && !world.has<z13::gameplay::Gameplay>()) {
    EndSession(world);
    SetConnectionStatus(world, ConnectionState::kNone);
    return;
  }

  // The transport belongs to the present, not to ticks being re-simulated.
  if (ft::IsCatchingUp(world)) {
    return;
  }

  NetSession& session = world.get_mut<NetSession>();
  if (world.has<ServerRole>()) {
    ServiceServer(world, session);
  } else {
    ServiceClient(world, session);
  }
}

void RegisterComponents(flecs::world world) {
  z13::flecs_tools::RegisterComponents<
      NetSession, ScheduledSessionDeltas, ClockSync, CommandRateLimits, PlayerIdAllocator, StateDigests,
      NetTuning, z13::ConnectTimeout, AdoptedSettings>(world);
}

void RegisterSystems(flecs::world world) {
  // Set here, not next to `.add(flecs::Singleton)` (see PhysicsSystem::RegisterSystems).
  world.set<ScheduledSessionDeltas>({});
  world.set<ClockSync>({});
  world.set<CommandRateLimits>({});
  world.set<PlayerIdAllocator>({});
  world.set<StateDigests>({});
  // The launcher overwrites these with the loaded settings (z13::InstallSettings).
  world.set<NetTuning>({});
  world.set<z13::ConnectTimeout>({});
  RegisterStateDigestSystems(world);

  world.observer<StartServerRequest, const TransportFactories>("NetSessionSystem::OnStartServerRequest")
      .event(flecs::OnSet)
      .each(OnStartServerRequest);

  world.observer<JoinRequest, const TransportFactories>("NetSessionSystem::OnJoinRequest")
      .event(flecs::OnSet)
      .each(OnJoinRequest);

  world.observer("NetSessionSystem::OnLeaveRequest")
      .with<LeaveRequest>()
      .event(flecs::OnAdd)
      .each(OnLeaveRequest);

  // PreFrame: earliest custom phase point, so a join/spawn/leave this tick is visible
  // to every gameplay system that runs later in the same frame.
  world.system("NetSessionSystem::Service")
      .kind(flecs::PreFrame)
      .immediate()
      .each([world]() { ServiceNetSession(world); });

  // PostFrame like the interval capture, so the snapshot's state and clock agree.
  world.system<ft::WorldSnapshotHistory, const ft::SimulationClock>("NetSessionSystem::EnsureBaselineSnapshot")
      .kind(flecs::PostFrame)
      .with<ServerRole>()
      .each([](flecs::iter& it, size_t, ft::WorldSnapshotHistory& history, const ft::SimulationClock& clock) {
        if (!history.history.Empty()) {
          return;
        }
        auto snapshot = ft::CaptureState(it.world());
        if (!snapshot) {
          log_error("NetSession(server): baseline snapshot failed: {}", snapshot.error());
          return;
        }
        history.history.Push({.tick = clock.tick, .snapshot = std::move(*snapshot)});
      });

  world.system<ClockSync, const ft::SimulationClock, const NetTuning>("NetSessionSystem::SteerClock")
      .kind(flecs::PostFrame)
      .with<ClientRole>()
      .without<ft::ReplayInProgress>()
      .each([](flecs::iter& it, size_t, ClockSync& sync, const ft::SimulationClock& clock, const NetTuning& tuning) {
        if (const int64_t adjust = TakeClockAdjustment(sync, tuning, clock.tick); adjust != 0) {
          flecs::world world = it.world();
          ft::RequestClockAdjust(world, adjust);
        }
      });
}

}  // namespace

void NetSessionSystem::Register(flecs::world& world) {
  z13::OnRegisterComponents(world, RegisterComponents);

  z13::OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::net
