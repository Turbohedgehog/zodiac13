#include "gameplay_input_system.h"

#include <z13_module/components/gameplay.h>
#include <z13_module/components/input.h>
#include <z13_module/components/geometry.h>

#include <lib_core/log.h>

#include <Eigen/Dense>

#include <flecs.h>

namespace z13::gameplay {

struct CameraQueryComponent {
  flecs::query<Camera, geometry::Transform> camera_query;
};

void OnMousePos(
    const input::MousePos& mouse_pos,
    const CameraQueryComponent& camera_query_component) {
  camera_query_component.camera_query.each([&mouse_pos](flecs::entity e, const Camera&, geometry::Transform& transform) {
    // LOG_INFO("~~~ {} OnMousePos = {}, {}", e.name().c_str(), mouse_pos.x, mouse_pos.y);
  });
}

void OnMouseMove(
    flecs::entity e,
    const input::MouseMoveEvent& mouse_move,
    const CameraQueryComponent& camera_query_component) {
  camera_query_component.camera_query.each([&mouse_move](flecs::entity e, Camera& camera, geometry::Transform& transform) {
    auto delta_time = e.world().delta_time();

    camera.h_rotation_deg += -static_cast<float>(mouse_move.delta.x) * delta_time * 2.f;
    camera.v_rotation_deg += static_cast<float>(mouse_move.delta.y) * delta_time * 2.f;

    camera.h_rotation_deg = std::fmodf(camera.h_rotation_deg, 360.f);
    camera.v_rotation_deg = std::clamp(camera.v_rotation_deg, -90.f, 90.f);

    // LOG_INFO("~~~ OnMouseMove = {}, {}", camera.h_rotation_deg, camera.v_rotation_deg);

    Eigen::Quaternionf rotation =
        Eigen::AngleAxisf(geometry::ToRadians(camera.h_rotation_deg), Eigen::Vector3f::UnitY()) *
        Eigen::AngleAxisf(geometry::ToRadians(camera.v_rotation_deg), Eigen::Vector3f::UnitX());

    rotation.normalize();

    transform.rotation = {
      .x = rotation.x(),
      .y = rotation.y(),
      .z = rotation.z(),
      .w = rotation.w(),
    };

    // LOG_INFO("~~~ {} OnMouseMove = {}, {}", e.name().c_str(), mouse_move.delta.x, mouse_move.delta.y);
  });
}

void OnMouseDown(
    const input::MouseButtonDownEvent& mouse_down,
    const CameraQueryComponent& camera_query_component) {
}

void OnMouseUp(
    const input::MouseButtonUpEvent& mouse_up,
    const CameraQueryComponent& camera_query_component) {
}

void OnKeyboardDown(
    const input::KeyboardDownEvent& key_down,
    const CameraQueryComponent& camera_query_component) {
}

void OnKeyboardUp(
    const input::KeyboardUpEvent& key_up,
    const CameraQueryComponent& camera_query_component) {
}

void OnMousePosEvent(flecs::entity e, input::MousePos& mp) {
  // LOG_INFO("==== {} OnMousePosEvent = {}, {} -> {}", mp.idx, e.name().c_str(), mp.x, mp.y, e.world().count<input::MousePos>());
}

void GameplayInputSystem::Register(flecs::world& world) {
  world.component<CameraQueryComponent>().add(flecs::Singleton);

  world.observer<input::MousePos, CameraQueryComponent>("OnMousePosObserver")
    .event<input::SystemInputEvent>()
    .each(OnMousePos);

  world.observer<input::MouseMoveEvent, CameraQueryComponent>("OnMouseMoveObserver")
    .event<input::SystemInputEvent>()
    .each(OnMouseMove);

  world.observer<input::MouseButtonDownEvent, CameraQueryComponent>("OnMouseDownObserver")
    .event<input::SystemInputEvent>()
    .each(OnMouseDown);

  world.observer<input::MouseButtonUpEvent, CameraQueryComponent>("OnMouseUpObserver")
    .event<input::SystemInputEvent>()
    .each(OnMouseUp);

  world.observer<input::KeyboardDownEvent, CameraQueryComponent>("OnKeyboardDownObserver")
    .event<input::SystemInputEvent>()
    .each(OnKeyboardDown);

  world.observer<input::KeyboardUpEvent, CameraQueryComponent>("OnKeyboardUpObserver")
    .event<input::SystemInputEvent>()
    .each(OnKeyboardUp);

  CameraQueryComponent camera_query_component = {
    .camera_query = world.query_builder<Camera, geometry::Transform>("CameraQuery").build(),
  };
  world.set(camera_query_component);
}

}  // namespace z13::gameplay
