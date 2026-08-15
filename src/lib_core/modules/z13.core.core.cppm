// z13.core.core module partition unit.
// Перенесён из include/lib_core/core.h.

module;

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <flecs.h>

export module z13.core.core;

import z13.core.core_types;
import z13.core.config;

export namespace z13 {

class Core {
 public:
  Core(int argc, char* argv[]);
  ~Core();  // for forward declared unique_ptr
  const Config& GetConfig() const;
  bool RegisterModuleFactory(ModuleFactoryPtr module_factory);
  WorldRef CreateWorld();

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
  Config config_;

  std::map<WorldId, flecs::world> worlds_;
  WorldId new_world_id_ = 0;
  std::vector<ModuleFactoryPtr> module_factories_;
  bool pending_shutdown_ = false;
  std::unique_ptr<ModuleLibHolder> module_lib_holder_;
};

}  // namespace z13