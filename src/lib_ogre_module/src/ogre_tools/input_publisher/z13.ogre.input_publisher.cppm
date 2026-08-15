// z13.ogre.input_publisher module interface unit.
// Внутренний модуль DLL lib_ogre_module.
// Перенесён из src/ogre_tools/input_publisher/input_publisher.h.

module;

#include <flecs.h>
#include <OgreInput.h>

export module z13.ogre.input_publisher;

export namespace z13::ogre {

class InputPublisher {
 public:
  static bool PublishInput(flecs::world world, const OgreBites::Event& ogre_event);
};

}  // namespace z13::ogre