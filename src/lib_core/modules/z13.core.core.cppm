// z13.core.core module partition unit.
// Перенесён из include/lib_core/core.h.
//
// Класс Core использует PIMPL (CoreImpl) для скрытия flecs::world и Config
// из экспортируемого интерфейса модуля. Это необходимо, т.к. типы из flecs.h
// и lib_core/config.h не могут экспортироваться через границы модулей
// (шаблонные специализации flecs несовместимы с экспортом из модуля — C1116).

module;

#include <filesystem>
#include <memory>
#include <utility>
#include <vector>

export module z13.core.core;

import z13.core.core_types;
import z13.core.config;

export namespace z13 {

class CoreImpl;

class Core {
 public:
  Core(int argc, char* argv[]);
  ~Core();  // for forward declared unique_ptr
  const Config& GetConfig() const;
  WorldId CreateWorld();
  bool RegisterModuleFactory(ModuleFactoryPtr module_factory);

  template <typename T, typename... Ts>
  bool RegisterModuleFactory(Ts&&... params) {
    return RegisterModuleFactory(std::make_shared<T>(std::forward<Ts>(params)...));
  }

  bool RegisterModuleFactory(const std::filesystem::path& module_lib_path, bool append_platform_extension = true);

  void Update(float delta_time);
  int Run();
  void Shutdown();
  bool IsPendingShutDown() const;

 private:
  std::unique_ptr<CoreImpl> impl_;
};

}  // namespace z13