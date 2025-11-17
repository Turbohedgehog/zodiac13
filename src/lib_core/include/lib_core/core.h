#pragma once

#include <map>
#include <string>
#include <memory>
#include <vector>

#include "core_types.h"
#include "config.h"

#include <flecs.h>

namespace z13 {

class Core {
 public:
  Core(int argc, char *argv[]);
  const Config& GetConfig() const;
  bool RegisterModuleFactory(ModuleFactoryPtr module_factory);
  WorldRef CreateWorld();

  template <typename T, typename... Ts>
  bool RegisterModuleFactory(Ts&&... params) {
    return RegisterModuleFactory(std::make_shared<T>(std::forward<Ts>(params)...));
  }

  void Update(float delta_time);
  int Run();
  void Shutdown();
  bool IsPendingShutDown() const;

 private:
  Config config_;

  std::map<WorldId, flecs::world> worlds_;
  WorldId new_world_id_ = 0;
  // std::map<std::string, ModuleFactoryPtr> module_factories_;
  std::vector<ModuleFactoryPtr> module_factories_;
  bool pending_shutdown_ = false;
};

}  // namespace z13
