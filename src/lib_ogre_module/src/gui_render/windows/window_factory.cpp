#include "window_factory.h"

#include "gameplay_main_menu.h"
#include "options_input_keyboard_bindings.h"
#include "options_input_settings.h"

namespace z13::ogre::gui {

WindowPtr WindowFactory::CreateGameplayMainMenu(flecs::world world) {
  return std::make_shared<GameplayMainMenuWindow>(world);
}

WindowPtr WindowFactory::CreateInputSettingsMenu(flecs::world world) {
  return std::make_shared<InputSettingsWindow>(world);
}

WindowPtr WindowFactory::CreateKeyboardBindingsMenu(flecs::world world) {
  return std::make_shared<KeyBindingWindow>(world);
}

}  // namespace z13::ogre::gui
