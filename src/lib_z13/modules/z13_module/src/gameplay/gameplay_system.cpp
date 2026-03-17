#include "gameplay_system.h"

#include <z13/components/gameplay.h>
#include <z13/components/input.h>
#include <z13/components/geometry.h>

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

void CreateTestActor(flecs::world world) {
  auto test_actor_entity = world.entity("TestEntity");
  Camera camera {
    .fov = 90,
    .name = "TestActorCamera",
  };
  geometry::Transform camera_transform = geometry::Transform::kIdentity;
  test_actor_entity
      .set(camera)
      .set(camera_transform)
      .add<z13::input::InputListener>()
      .add<z13::input::ActionListener>()
      .add<z13::input::CurrentActionListenerTag>();
  // camera_entity.add<input::SystemInputEvent>();
}

void OnInit(flecs::world world, const gameplay::Gameplay&) {
  CreateTestActor(world);

  LOG_INFO("~~~~ gameplay::OnInit");
}

void GameplaySystem::Register(flecs::world& world) {
  RegisterPipeline(world);

  world.observer<const gameplay::Gameplay>("GameplaySystem::OnInit")
    .event(flecs::OnAdd)
    .yield_existing()
    .each([world](const auto& gameplay) { OnInit(world, gameplay); });

  world.observer<const WindowFocusEvent>("WindowFocusEvent::OnSet")
    .event(flecs::OnSet)
    .yield_existing()
    .each([world](const auto& window_focus_event) {
      if (!window_focus_event.has_focus) {
        world.add<Pause>();
      }
    });

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
