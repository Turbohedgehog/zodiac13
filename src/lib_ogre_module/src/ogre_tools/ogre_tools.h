#pragma once

#include <flecs.h>

namespace z13::gameplay {

struct Gameplay;
struct Pause;
struct Camera;

}  // namespace z13::gameplay

namespace z13::geometry {

struct Transform;

}  // namespace z13::geometry

namespace z13::input {

struct SystemInputEvent;

}  // namespace z13::input

namespace z13::ogre {

struct OgreData;
// struct SdlInput;

class OgreTools {
 public:
  static void CreateSdlOgreRoot(flecs::world world, OgreData& ogre_data);
  static void ReadSdlEvents(flecs::world world, OgreData& ogre_data);
  static void RenderSdlOgreWindow(flecs::world world, OgreData& ogre_data);
  static void DestroySdlOgreWindow(OgreData& ogre_data);
  static void EnableRelativeMouseMode(const gameplay::Pause*);
  static void DisableRelativeMouseMode(const OgreData& ogre_data, const gameplay::Pause&);
  static void CreateCamera(const gameplay::Camera& camera, OgreData& ogre_data);
  static void UpdateCamera(const gameplay::Camera& camera, const geometry::Transform& transform, OgreData& ogre_data);
};

}  // namespace z13::ogre
