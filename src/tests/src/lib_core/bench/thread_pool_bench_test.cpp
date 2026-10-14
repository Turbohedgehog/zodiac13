/*
 * Copyright 2026 Ivan Kulenko / Zodiac13
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://apache.org
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <iostream>
#include <numeric>
#include <thread>
#include <string_view>
#include <vector>

#include <lib_core/utils/thread_pool.h>

// ThreadPool against the serial loop over the same work, at 1-4 threads. The libraries it was
// chosen among and their numbers: docs/station-systems-plan.md, stage 18.
namespace z13 {
namespace {

using Body = std::function<void(size_t)>;
using ForEachIndex = std::function<void(size_t, const Body&)>;

constexpr std::array kThreadCounts = {size_t {1}, size_t {2}, size_t {3}, size_t {4}};
// The median of these runs is reported: the PRoot box drifts between runs.
constexpr int kRepeats = 7;

// Deterministic integer work, so every candidate must produce the serial result.
uint64_t Work(size_t index, size_t iterations) {
  uint64_t x = index + 1;
  for (size_t i = 0; i < iterations; ++i) {
    x = x * 6364136223846793005ULL + 1442695040888963407ULL;
    x ^= x >> 33;
  }
  return x;
}

struct Scenario {
  std::string_view name;
  // Indices per call, calls per measurement.
  size_t count {};
  size_t calls {};
  std::function<size_t(size_t)> iterations;
  // Untimed idle before each call, long enough for every pool to fall asleep.
  std::chrono::milliseconds pause {};
};

const std::array kScenarios = {
    // Many tiny tasks: the cost of handing out work.
    Scenario {"tiny tasks", 10000, 1, [](size_t) { return size_t {50}; }},
    // Few uneven chunks, like a station's chunks in a room rebuild: balancing.
    Scenario {"uneven chunks", 64, 1, [](size_t i) { return size_t {20000} * (1 + (i * 7) % 16); }},
    // Short loops back to back: the pool stays awake between them.
    Scenario {"back to back", 8, 1000, [](size_t) { return size_t {2000}; }},
    // A short loop once a tick: the pool sleeps before each call, so this is the wake-up's cost.
    Scenario {"once a tick", 8, 200, [](size_t) { return size_t {2000}; }, std::chrono::milliseconds(2)},
};

struct Candidate {
  std::string_view name;
  std::function<ForEachIndex(size_t threads)> make;
};

ForEachIndex MakeSerial(size_t) {
  return [](size_t count, const Body& body) {
    for (size_t i = 0; i < count; ++i) {
      body(i);
    }
  };
}

ForEachIndex MakeThreadPool(size_t threads) {
  return [pool = ThreadPool(threads)](size_t count, const Body& body) { pool.ForEachIndex(count, body); };
}

const std::array kCandidates = {
    Candidate {"serial", MakeSerial},
    Candidate {"ThreadPool", MakeThreadPool},
};

uint64_t Checksum(const std::vector<uint64_t>& results) {
  return std::accumulate(results.begin(), results.end(), uint64_t {}, std::bit_xor<>());
}

// Milliseconds for one measurement, the median of kRepeats; fails if a result differs.
double Measure(const ForEachIndex& for_each, const Scenario& scenario, uint64_t expected) {
  std::vector<double> times;
  std::vector<uint64_t> results(scenario.count);
  const Body body = [&](size_t i) { results[i] = Work(i, scenario.iterations(i)); };
  for (int repeat = 0; repeat < kRepeats; ++repeat) {
    std::ranges::fill(results, 0);
    std::chrono::steady_clock::duration total {};
    for (size_t call = 0; call < scenario.calls; ++call) {
      std::this_thread::sleep_for(scenario.pause);
      const auto start = std::chrono::steady_clock::now();
      for_each(scenario.count, body);
      total += std::chrono::steady_clock::now() - start;
    }
    times.push_back(std::chrono::duration<double, std::milli>(total).count());
    EXPECT_EQ(Checksum(results), expected) << scenario.name;
  }
  std::ranges::sort(times);
  return times[times.size() / 2];
}

TEST(ThreadPoolBench, DISABLED_Candidates) {
  for (const Scenario& scenario : kScenarios) {
    std::vector<uint64_t> serial(scenario.count);
    for (size_t i = 0; i < scenario.count; ++i) {
      serial[i] = Work(i, scenario.iterations(i));
    }
    const uint64_t expected = Checksum(serial);

    std::cout << std::format("\n{} ({} indices x {} calls), ms, median of {}\n", scenario.name, scenario.count,
                             scenario.calls, kRepeats);
    std::cout << std::format("{:<18}", "threads");
    for (const size_t threads : kThreadCounts) {
      std::cout << std::format("{:>10}", threads);
    }
    std::cout << "\n";
    for (const Candidate& candidate : kCandidates) {
      std::cout << std::format("{:<18}", candidate.name);
      for (const size_t threads : kThreadCounts) {
        const ForEachIndex for_each = candidate.make(threads);
        std::cout << std::format("{:>10.2f}", Measure(for_each, scenario, expected)) << std::flush;
      }
      std::cout << "\n";
    }
  }
}

}  // namespace
}  // namespace z13
