#pragma once

namespace z13::status {

struct OnStartupGameEvent {};

struct Z13State {
  bool shutdown = false;
};

struct Loading {
  float percent = 0.f;
};

}  // namespace z13::status
