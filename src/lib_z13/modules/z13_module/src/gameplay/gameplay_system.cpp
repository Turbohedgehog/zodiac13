#include "gameplay_system.h"

#include <z13_module/components/gameplay.h>
#include <z13_module/components/input.h>
#include <z13_module/components/geometry.h>

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

void CreateCameraActor(flecs::world world) {
  auto camera_entity = world.entity("CameraEntity");
  Camera camera {
    .fov = 90,
    .name = "MainCamera",
  };
  geometry::Transform camera_transform = geometry::Transform::kIdentity;
  camera_entity.set(camera).set(camera_transform);
  // camera_entity.add<input::SystemInputEvent>();
}

void OnInit(flecs::world world, const gameplay::Gameplay&) {
  CreateCameraActor(world);
  // world.entity().add<input::SystemInputEvent>();

  // e.world().entity().set(camera);
  LOG_INFO("~~~~ gameplay::OnInit");
}

void GameplaySystem::Register(flecs::world& world) {
  RegisterPipeline(world);

  world.observer<const gameplay::Gameplay>("GameplaySystem::OnInit")
    // .term_at(0).singleton()
    .event(flecs::OnAdd)
    .yield_existing()
    .each([world](const auto& gameplay) { OnInit(world, gameplay); });

  world.system("UpdateGameplaySystem")
    .kind<Update>()
    .each(UpdateGameplay);

  world.system("OnGameplaySystem")
    .kind<Update>()
    .each(OnGameplay);

  world.system("ValidateGameplaySystem")
    .kind(flecs::OnValidate)
    .each(ValidateGameplay);
}

}  // namespace z13::gameplay
