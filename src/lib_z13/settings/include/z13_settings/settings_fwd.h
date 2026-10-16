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

// settings.fbs's tables, declared without settings_generated.h, which brings every settings
// schema along: enough for signatures that only pass them on (settings.h has the definitions).
namespace z13::fbs::settings {
struct ConnectTimeoutT;
struct SessionTimeoutT;
struct SettingsT;
}  // namespace z13::fbs::settings

namespace z13 {

using ConnectTimeoutConfig = fbs::settings::ConnectTimeoutT;
using SessionTimeoutConfig = fbs::settings::SessionTimeoutT;
using Settings = fbs::settings::SettingsT;

}  // namespace z13
