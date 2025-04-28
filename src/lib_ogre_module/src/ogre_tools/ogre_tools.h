#pragma once

namespace z13::ogre {

class OgreTools {
 public:
  static void CreateSDLOgreRoot(struct OgreData& ogre_data);
  static void UpdateSDLOgreWindow(struct OgreData& ogre_data);
  static void DestroySDLOgreWindow(struct OgreData& ogre_data);
};

}  // namespace z13::ogre
