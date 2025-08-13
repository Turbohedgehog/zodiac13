#pragma once

#include <optional>

#include <lib_core/core_types.h>

namespace z13 {

struct PendingDestroy {};

struct CoreComponent {
  std::optional<CoreRef> core;
};

}  // namespace z13
