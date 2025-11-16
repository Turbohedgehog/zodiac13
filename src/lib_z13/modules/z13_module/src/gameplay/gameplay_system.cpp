#include "gameplay_system.h"

#include <z13_module/components/gameplay.h>

#include <flecs.h>

#include <lib_core/log.h>

namespace z13::gameplay {

struct Update {};

void RegisterPipeline(flecs::world& world) {
  auto update_phase = world.component<Update>().add(flecs::Phase).depends_on(flecs::OnUpdate);
  world.get_alive(flecs::OnValidate).add(flecs::Phase).depends_on(update_phase);
}

void UpdateGameplay() {
  // LOG_INFO("UpdateGameplay()");
}

void OnGameplay() {
  // LOG_INFO("Gameplay()");
}

void ValidateGameplay() {
  // LOG_INFO("ValidateGameplay()");
}

// void OnInit(flecs::iter& iter, size_t i, const gameplay::Gameplay&) {
void OnInit(flecs::iter& iter, size_t i, const gameplay::Gameplay&) {
  Camera camera {
    .position = geometry::Vector3::kZero,
    .orientation = geometry::Quaternion::kIdentity,
  };

  iter.world().entity("GameplayCamera").set(camera);

  // e.world().entity().set(camera);
  LOG_INFO("~~~~ gameplay::OnInit");
}

void GameplaySystem::Register(flecs::world& world) {
  RegisterPipeline(world);

  world.observer<const gameplay::Gameplay>()
    .term_at(0).singleton()
    .event(flecs::OnAdd)
    .each(OnInit);

  world.system()
    .kind<Update>()
    .each(UpdateGameplay);

  world.system()
    .kind<Update>()
    .each(OnGameplay);

  world.system()
    .kind(flecs::OnValidate)
    .each(ValidateGameplay);
}

}  // namespace z13::gameplay
