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

#pragma once

// Tracy zones, compiled in only with Z13_PROFILE (make.py --profile); see the perf-measure skill.
// A zone's name is a string literal: Tracy keeps the pointer.
#ifdef Z13_PROFILE
#include <tracy/Tracy.hpp>
#define Z13_PROFILE_ZONE(name) ZoneScopedN(name)
#define Z13_PROFILE_FRAME() FrameMark
#else
#define Z13_PROFILE_ZONE(name)
#define Z13_PROFILE_FRAME()
#endif
