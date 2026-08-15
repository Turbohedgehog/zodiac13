// z13.core.components module partition unit.
// Перенесён из include/lib_core/components.h.

module;

#include <optional>

export module z13.core.components;

import z13.core.core_types;

export namespace z13 {

struct PendingDestroy {};

struct CoreComponent {
  std::optional<CoreRef> core;
};

struct RegisterComponentsEvent {};
struct InitPhasesEvent {};
struct InitSystemsEvent {};
struct InitWorldDataEvent {};

}  // namespace z13