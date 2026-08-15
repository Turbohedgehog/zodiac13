// z13.core.flecs_utils module partition unit.
// Перенесён из include/lib_core/flecs_utils.h.
//
// WorldNoDeferGuard использует PIMPL, чтобы скрыть flecs::world из
// экспортируемого интерфейса модуля (шаблонные специализации flecs
// несовместимы с экспортом из модуля — C1116). Конструктор принимает
// flecs::world_t* (C-тип), т.к. flecs::world неявно конвертируется в него.

module;

#include <memory>

#include <flecs.h>

export module z13.core.flecs_utils;

export namespace z13 {

class WorldNoDeferGuardImpl;

class WorldNoDeferGuard {
 public:
  explicit WorldNoDeferGuard(::flecs::world_t* world);
  ~WorldNoDeferGuard();

  WorldNoDeferGuard(const WorldNoDeferGuard&) = delete;
  WorldNoDeferGuard& operator=(const WorldNoDeferGuard&) = delete;

 private:
  std::unique_ptr<WorldNoDeferGuardImpl> impl_;
};

}  // namespace z13