// z13.components module interface unit.
// Перенесён из include/z13/components/*.h.
//
// Сюда НЕ входят типы из include/z13/components/input.h, зависящие от
// сгенерированных flatbuffers-типов (z13::fbs::input::Keycode, actions и т.д.) —
// они не экспортируются из модуля (см. план миграции, раздел 4.3, вариант в)
// и остаются в классических заголовках.

module;

#include <string>

export module z13.components;

export namespace z13::bootstrap {

struct LoadConfigEvent {};
struct CreatePlayerEvent {};

}  // namespace z13::bootstrap

export namespace z13::building {

struct BuildingTool {
};

struct Brush {
  float distance {};
};

struct BasicBlock {
};

}  // namespace z13::building

export namespace z13::gameplay {

struct PreUpdatePhase {};
struct UpdatePhase {};
struct PostUpdatePhase {};

struct Gameplay {
  uint32_t last_registered_player_id {};
};

struct Pause {};

struct WindowFocusEvent {
  bool has_focus = false;
};

struct Player {
  uint32_t id {};
};

struct Camera {
  float fov = 90.f;
  std::string name;
};

}  // namespace z13::gameplay

export namespace z13::status {

struct OnStartupGameEvent {};

struct Z13State {
  bool shutdown = false;
};

struct Loading {
  float percent {};
};

}  // namespace z13::status

export namespace z13 {

struct PlayerInfoComponent {
  uint32_t id {};
  std::string login;
  std::string name;
};

}  // namespace z13

export namespace z13::constants {

inline const std::string kGameName = "zodiac13";

}  // namespace z13::constants