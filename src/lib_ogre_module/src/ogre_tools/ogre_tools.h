#pragma once

#include <flecs.h>

namespace z13::gameplay {

struct Gameplay;
struct Pause;

}  // namespace z13::gameplay

namespace z13::ogre {

struct OgreData;
struct SdlInput;

class OgreTools {
 public:
  static void CreateSdlOgreRoot(flecs::world world, OgreData& ogre_data, SdlInput& sdl_input);
  static void ReadSdlEvents(flecs::entity e, OgreData& ogre_data, SdlInput& sdl_input);
  static void RenderSdlOgreWindow(flecs::entity e, OgreData& ogre_data);
  static void DestroySdlOgreWindow(OgreData& ogre_data, SdlInput& sdl_input);
  static void EnableRelativeMouseMode(const gameplay::Pause*);
  static void DisableRelativeMouseMode(const gameplay::Pause*);
};

}  // namespace z13::ogre
