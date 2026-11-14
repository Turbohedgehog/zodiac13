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


#include "console_window.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <console_input/console_completion.h>
#include <console_input/console_session.h>
#include <lib_core/commands/command_registry.h>
#include <lib_core/utils/console_log.h>
#include <lib_core/utils/log.h>
#include <lib_core/utils/text_lines.h>
#include <z13/components/console/assistant_chat.h>

#include "gui_widgets.h"

namespace z13::raylib::gui {

namespace {

constexpr float kHeightFraction = 0.5f;
constexpr float kSlideSeconds = 0.18f;
constexpr float kBackgroundAlpha = 0.88f;
constexpr float kButtonSizeEm = 3.f;
constexpr float kButtonMarginEm = 0.75f;
constexpr char kConsoleCharacter = '`';
constexpr std::string_view kTabsId = "##console_tabs";
constexpr std::string_view kCommandField = "##console_command";
constexpr std::string_view kSuggestionsWindow = "##console_suggestions";
constexpr float kSuggestionsHeightEm = 12.f;
constexpr std::string_view kChatField = "##console_chat";
constexpr std::string_view kNoAssistantText =
    "No assistant is following this chat. Ask it to run tools/mcp/console_listen.py: what you write waits for it.";
constexpr std::string_view kYouPrefix = "you: ";
constexpr std::string_view kAssistantPrefix = "assistant: ";
constexpr std::string_view kAssistantHereText = "The assistant is following this chat.";

constexpr ImVec4 kDebugColor {0.55f, 0.55f, 0.55f, 1.f};
constexpr ImVec4 kInfoColor {0.9f, 0.9f, 0.9f, 1.f};
constexpr ImVec4 kWarningColor {1.f, 0.85f, 0.4f, 1.f};
constexpr ImVec4 kPlayerColor {0.6f, 0.85f, 1.f, 1.f};
constexpr ImVec4 kAssistantColor {0.7f, 1.f, 0.7f, 1.f};
constexpr ImVec4 kSeenColor {0.55f, 0.6f, 0.7f, 1.f};

struct Context {
  flecs::world world;
  ConsoleState& console;
};

ImVec4 ColorOf(spdlog::level::level_enum level) {
  switch (level) {
    case spdlog::level::trace:
      [[fallthrough]];
    case spdlog::level::debug:
      return kDebugColor;
    case spdlog::level::warn:
      return kWarningColor;
    case spdlog::level::err:
      [[fallthrough]];
    case spdlog::level::critical:
      return kErrorColor;
    default:
      return kInfoColor;
  }
}

void ReplaceText(ImGuiInputTextCallbackData& data, std::string_view text) {
  data.DeleteChars(0, data.BufTextLen);
  data.InsertChars(0, text.data(), text.data() + text.size());
}

void StepHistory(ImGuiInputTextCallbackData& data, const std::vector<std::string>& history,
                 std::optional<std::size_t>& position) {
  if (history.empty()) {
    return;
  }
  if (data.EventKey == ImGuiKey_UpArrow) {
    position = position ? std::max<std::size_t>(*position, 1) - 1 : history.size() - 1;
  } else if (position && *position + 1 < history.size()) {
    ++*position;
  } else {
    position.reset();
  }
  ReplaceText(data, position ? std::string_view(history[*position]) : "");
}

// Tab: the one candidate, or the start all share and the list of them; with the list open, the chosen one.
std::string Complete(flecs::world world, ConsoleState& console, std::string_view line) {
  std::string completed(line);
  if (console.suggestions) {
    const console_input::Completion& offered = *console.suggestions;
    completed = console_input::Completed(line, offered, offered.candidates[console.suggestion]);
    console.suggestions.reset();
  } else if (console_input::Completion completion = console_input::CompletionOf(world, line);
             completion.candidates.size() == 1) {
    completed = console_input::Completed(line, completion, completion.candidates.front());
  } else if (!completion.candidates.empty()) {
    completed = console_input::Replaced(line, completion, console_input::CommonPrefix(completion));
    console.suggestions = std::move(completion);
    console.suggestion = 0;
    console.suggestion_moved = true;
  }
  console.suggestions_line = completed;
  return completed;
}

void StepSuggestion(const ImGuiInputTextCallbackData& data, ConsoleState& console) {
  const std::size_t count = console.suggestions->candidates.size();
  console.suggestion = data.EventKey == ImGuiKey_UpArrow ? (console.suggestion + count - 1) % count
                                                         : (console.suggestion + 1) % count;
  console.suggestion_moved = true;
}

void ResetLine(const ImGuiInputTextCallbackData& data, ConsoleState& console) {
  console_input::ResetLineCompletion(console.line_completion,
                                     std::string_view(data.Buf, static_cast<std::size_t>(data.BufTextLen)));
}

// The rules of what typing and deleting show are in console_input::StepLineCompletion.
void UpdateCompletion(ImGuiInputTextCallbackData& data, const Context& context) {
  ConsoleState& console = context.console;
  if (console.cursor_to_end) {
    data.CursorPos = data.BufTextLen;
    data.SelectionStart = data.BufTextLen;
    data.SelectionEnd = data.BufTextLen;
    console.cursor_to_end = false;
    ResetLine(data, console);
    return;
  }
  const std::string text(data.Buf, static_cast<std::size_t>(data.BufTextLen));
  const bool at_end = data.CursorPos == data.BufTextLen && data.SelectionStart == data.SelectionEnd;
  const console_input::LineCompletionStep step = console_input::StepLineCompletion(
      console.line_completion, text, at_end,
      [&context](std::string_view line) { return console_input::CompletionOf(context.world, line); });
  if (!step.changed || !step.completes) {
    return;
  }
  console.suggestions = step.list;
  if (step.list) {
    console.suggestions_line = text;
    console.suggestion = 0;
    console.suggestion_moved = true;
  }
  if (!step.rest.empty()) {
    const int typed_end = data.BufTextLen;
    data.InsertChars(typed_end, step.rest.data(), step.rest.data() + step.rest.size());
    data.SelectionStart = typed_end;
    data.SelectionEnd = data.BufTextLen;
    data.CursorPos = data.BufTextLen;
  }
}

int EditCommand(ImGuiInputTextCallbackData* data) {
  auto& context = *static_cast<Context*>(data->UserData);
  switch (data->EventFlag) {
    case ImGuiInputTextFlags_CallbackCharFilter:
      return data->EventChar == kConsoleCharacter ? 1 : 0;
    case ImGuiInputTextFlags_CallbackHistory:
      if (context.console.suggestions) {
        StepSuggestion(*data, context.console);
      } else {
        StepHistory(*data, context.console.history, context.console.history_position);
        ResetLine(*data, context.console);
      }
      break;
    case ImGuiInputTextFlags_CallbackAlways:
      UpdateCompletion(*data, context);
      break;
    case ImGuiInputTextFlags_CallbackCompletion:
      ReplaceText(*data, Complete(context.world, context.console,
                                  std::string_view(data->Buf, static_cast<std::size_t>(data->BufTextLen))));
      ResetLine(*data, context.console);
      break;
    default:
      break;
  }
  return 0;
}

int EditChat(ImGuiInputTextCallbackData* data) {
  auto& console = static_cast<Context*>(data->UserData)->console;
  if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory) {
    StepHistory(*data, console.chat_history, console.chat_history_position);
    return 0;
  }
  return data->EventChar == kConsoleCharacter ? 1 : 0;
}

void SubmitTyped(const Context& context) {
  ConsoleState& console = context.console;
  if (!console.command_input.empty() && (console.history.empty() || console.history.back() != console.command_input)) {
    console.history.push_back(console.command_input);
  }
  console.history_position.reset();
  console.queued_lines.push_back(console.command_input);
  console.command_input.clear();
}

// A field that keeps the focus after Enter, and takes it when the console opens.
// A window a click on another one would lift above this one stays on top: the console is over every screen.
void KeepOnTop() {
  ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
}

void FocusIfAsked(ConsoleState& console) {
  if (console.focus_input) {
    ImGui::SetKeyboardFocusHere();
    console.focus_input = false;
  }
}

void NoteKeyboardRequest(ConsoleState& console) {
  if (ImGui::IsItemActivated()) {
    console.keyboard = true;
  }
}

// The view's lines again when the source changed, unless the player is selecting in them; a view that was at its
// end stays there.
template <typename Lines>
void Refresh(ReportView& view, std::uint64_t& applied, std::uint64_t revision, Lines make_lines) {
  if (revision == applied || view.HasSelection()) {
    return;
  }
  const bool follow = view.AtEnd();
  auto [lines, colors] = make_lines();
  view.SetLines(std::move(lines), std::move(colors));
  if (follow) {
    view.ScrollToEnd();
  }
  applied = revision;
}

struct ColouredLines {
  std::vector<std::string> lines;
  std::vector<ImU32> colors;
};

void AddLines(ColouredLines& out, std::string_view prefix, const std::string& text, ImU32 color) {
  const std::vector<std::string> parts = z13::SplitLines(text);
  for (std::size_t i = 0; i < parts.size(); ++i) {
    out.lines.push_back(i == 0 ? std::format("{}{}", prefix, parts[i]) : parts[i]);
    out.colors.push_back(color);
  }
}

void DrawCopyableView(ReportView& view) {
  view.Draw({0.f, -2.f * ImGui::GetFrameHeightWithSpacing()});
  ImGui::BeginDisabled(!view.HasSelection());
  if (ImGui::SmallButton("Copy selection")) {
    ImGui::SetClipboardText(view.Selection().c_str());
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::SmallButton("Copy all")) {
    ImGui::SetClipboardText(view.AllText().c_str());
  }
  if (!view.HasSelection()) {
    ImGui::SameLine();
    ImGui::TextDisabled("Long press a line to select");
  }
}

// The arrows move in the list, a touch or Enter takes a choice; editing the line narrows or widens it.
void DrawSuggestions(const Context& context) {
  ConsoleState& console = context.console;
  if (!console.suggestions) {
    return;
  }
  if (console.command_input != console.suggestions_line) {
    console_input::Completion again = console_input::CompletionOf(context.world, console.command_input);
    console.suggestions_line = console.command_input;
    console.suggestion = 0;
    console.suggestion_moved = true;
    if (again.candidates.size() < 2) {
      console.suggestions.reset();
      return;
    }
    console.suggestions = std::move(again);
  }
  const ImVec2 field_min = ImGui::GetItemRectMin();
  // Above the field, over the log: below it the list would sit where a soft keyboard covers the screen.
  ImGui::SetNextWindowPos({field_min.x, field_min.y}, ImGuiCond_Always, {0.f, 1.f});
  ImGui::SetNextWindowSizeConstraints({0.f, 0.f},
                                      {ImGui::GetIO().DisplaySize.x - field_min.x,
                                       std::min(Em(kSuggestionsHeightEm), field_min.y)});
  constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                      ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
                                      ImGuiWindowFlags_AlwaysAutoResize;
  std::optional<std::size_t> chosen;
  if (ImGui::Begin(kSuggestionsWindow.data(), nullptr, kFlags)) {
    KeepOnTop();
    // A pressed entry is the active item, which would make the window not hovered without the flag.
    const bool over_list = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Left) && (console.suggestions_dragged || over_list)) {
      console.suggestions_dragged = true;
      ImGui::SetScrollY(ImGui::GetScrollY() - ImGui::GetIO().MouseDelta.y);
    }
    const std::vector<std::string>& candidates = console.suggestions->candidates;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
      const bool current = i == console.suggestion;
      if (ImGui::Selectable(candidates[i].c_str(), current)) {
        chosen = i;
      }
      if (current && console.suggestion_moved) {
        ImGui::SetScrollHereY();
      }
    }
    console.suggestion_moved = false;
  }
  ImGui::End();
  if (console.suggestions_dragged) {
    chosen.reset();
    console.suggestions_dragged = ImGui::IsMouseDown(ImGuiMouseButton_Left);
  }
  if (chosen) {
    console.command_input = console_input::Completed(console.command_input, *console.suggestions,
                                                     console.suggestions->candidates[*chosen]);
    console.suggestions.reset();
    console.focus_input = true;
  }
}

void DrawLogTab(Context& context) {
  ConsoleState& console = context.console;
  const ConsoleLog& log = context.world.get<ConsoleLog>();
  Refresh(console.log_view, console.log_revision, log.Revision(), [&log] {
    ColouredLines out;
    for (const LogLine& line : log.Lines()) {
      AddLines(out, "", line.text, ImGui::ColorConvertFloat4ToU32(ColorOf(line.level)));
    }
    return out;
  });
  DrawCopyableView(console.log_view);
  ImGui::SetNextItemWidth(-1.f);
  console.cursor_to_end = console.cursor_to_end || console.focus_input;
  FocusIfAsked(console);
  constexpr ImGuiInputTextFlags kFlags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory |
                                         ImGuiInputTextFlags_CallbackCompletion |
                                         ImGuiInputTextFlags_CallbackCharFilter |
                                         ImGuiInputTextFlags_CallbackAlways;
  const bool entered =
      ImGui::InputText(kCommandField.data(), &console.command_input, kFlags, EditCommand, &context);
  NoteKeyboardRequest(console);
  DrawSuggestions(context);
  if (entered && console.suggestions) {
    console.command_input = Complete(context.world, console, console.command_input);
    console.focus_input = true;
  } else if (entered) {
    SubmitTyped(context);
    console.focus_input = true;
  }
}

constexpr std::string_view kNotDeliveredText = "not delivered";

// Said: warning; given to the assistant: the player's colour; looked at: dimmed, like a read message.
const ImVec4& LineColor(z13::console::Delivery delivery) {
  switch (delivery) {
    case z13::console::Delivery::kSent:
      return kWarningColor;
    case z13::console::Delivery::kDelivered:
      return kPlayerColor;
    case z13::console::Delivery::kSeen:
      return kSeenColor;
  }
  return kPlayerColor;
}

void DrawAssistantTab(Context& context) {
  ConsoleState& console = context.console;
  if (!context.world.has<z13::console::AssistantChat>()) {
    ImGui::TextUnformatted("This game has no assistant channel.");
    return;
  }
  auto& chat = context.world.get_mut<z13::console::AssistantChat>();
  ImGui::TextColored(chat.listening ? kAssistantColor : kWarningColor, "%s",
                     std::string(chat.listening ? kAssistantHereText : kNoAssistantText).c_str());
  Refresh(console.chat_view, console.chat_revision, chat.revision, [&chat] {
    ColouredLines out;
    for (const z13::console::ChatLine& line : chat.lines) {
      const bool unsent = line.from_player && line.delivery == z13::console::Delivery::kSent;
      AddLines(out, line.from_player ? kYouPrefix : kAssistantPrefix,
               unsent ? std::format("{}  [{}]", line.text, kNotDeliveredText) : line.text,
               ImGui::ColorConvertFloat4ToU32(line.from_player ? LineColor(line.delivery) : kAssistantColor));
    }
    return out;
  });
  DrawCopyableView(console.chat_view);
  ImGui::SetNextItemWidth(-1.f);
  FocusIfAsked(console);
  constexpr ImGuiInputTextFlags kFlags =
      ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackCharFilter |
      ImGuiInputTextFlags_CallbackHistory;
  const bool entered =
      ImGui::InputText(kChatField.data(), &console.chat_input, kFlags, EditChat, &context);
  NoteKeyboardRequest(console);
  if (entered) {
    console.chat_history_position.reset();
    if (!console.chat_input.empty()) {
      if (console.chat_history.empty() || console.chat_history.back() != console.chat_input) {
        console.chat_history.push_back(console.chat_input);
      }
      z13::console::SayToAssistant(chat, std::move(console.chat_input));
    }
    console.chat_input.clear();
    console.focus_input = true;
  }
}

struct TabSpec {
  ConsoleTab id;
  std::string_view name;
  void (*draw)(Context&);
};

constexpr std::array kTabs {
    TabSpec {ConsoleTab::kLog, "Console", DrawLogTab},
    TabSpec {ConsoleTab::kAssistant, "Assistant", DrawAssistantTab},
};

void DrawTabs(Context& context) {
  if (!ImGui::BeginTabBar(kTabsId.data())) {
    return;
  }
  const bool next = ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Tab, false);
  for (size_t i = 0; i < kTabs.size(); ++i) {
    const TabSpec& tab = kTabs[i];
    const bool after_current = kTabs[(i + kTabs.size() - 1) % kTabs.size()].id == context.console.tab;
    const ImGuiTabItemFlags flags = next && after_current ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
    if (ImGui::BeginTabItem(tab.name.data(), nullptr, flags)) {
      context.console.tab = tab.id;
      tab.draw(context);
      ImGui::EndTabItem();
    }
  }
  ImGui::EndTabBar();
}

// A soft keyboard has no Tab key.
void DrawKeyboardButtons(const Context& context) {
  ConsoleState& console = context.console;
  if (ImGui::SmallButton(console.keyboard ? "Hide keyboard" : "Keyboard")) {
    console.keyboard = !console.keyboard;
    console.focus_input = console.keyboard;
  }
  if (console.tab == ConsoleTab::kLog) {
    ImGui::SameLine();
    if (ImGui::SmallButton("Tab")) {
      console.command_input = Complete(context.world, console, console.command_input);
      console.focus_input = true;
    }
  }
}

void DrawToggleButton(ConsoleState& console, bool touch_screen) {
  const ImVec2 display = ImGui::GetIO().DisplaySize;
  const float size = Em(kButtonSizeEm);
  const float margin = Em(kButtonMarginEm);
  ImGui::SetNextWindowPos({display.x - size - margin, margin});
  ImGui::SetNextWindowBgAlpha(0.f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.f, 0.f});
  constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                      ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
  if (ImGui::Begin("##console_button", nullptr, kFlags)) {
    KeepOnTop();
    if (ImGui::Button(console.open ? "x" : ">_", {size, size})) {
      ToggleConsole(console, touch_screen);
    }
    const ImVec2 position = ImGui::GetWindowPos();
    const ImVec2 extent = ImGui::GetWindowSize();
    const Eigen::Vector2f screen(display.x, display.y);
    console.button_area = Eigen::AlignedBox2f(Eigen::Vector2f(position.x, position.y).cwiseQuotient(screen),
                                              Eigen::Vector2f(position.x + extent.x, position.y + extent.y)
                                                  .cwiseQuotient(screen));
  }
  ImGui::End();
  ImGui::PopStyleVar();
}

void KeepSoftKeyboard(const ConsoleState& console, SDL_Window* window) {
  if (window == nullptr) {
    return;
  }
  const bool wanted = console.open && console.keyboard;
  if (wanted != SDL_TextInputActive(window)) {
    if (wanted) {
      SDL_StartTextInput(window);
    } else {
      SDL_StopTextInput(window);
    }
  }
}

}  // namespace

bool IsTouchScreen(const z13::input::TouchControls* touch) {
#ifdef __ANDROID__
  return true;
#else
  return touch != nullptr && touch->used;
#endif
}

void ToggleConsole(ConsoleState& console, bool touch_screen) {
  console.open = !console.open;
  if (console.open) {
    console.focus_input = true;
    console.keyboard = touch_screen;
  }
}

void DrawConsole(flecs::world world, ConsoleState& console, SDL_Window* window, bool touch_screen) {
  const ImGuiIO& io = ImGui::GetIO();
  if (console.open && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
    if (console.suggestions) {
      console.suggestions.reset();
    } else {
      console.open = false;
    }
  }
  const float step = io.DeltaTime / kSlideSeconds;
  console.slide = std::clamp(console.slide + (console.open ? step : -step), 0.f, 1.f);
  if (touch_screen) {
    KeepSoftKeyboard(console, window);
  }
  if (console.slide > 0.f) {
    const float height = io.DisplaySize.y * kHeightFraction;
    ImGui::SetNextWindowPos({0.f, -height * (1.f - console.slide)});
    ImGui::SetNextWindowSize({io.DisplaySize.x, height});
    ImGui::SetNextWindowBgAlpha(kBackgroundAlpha);
    constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                        ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin("##console", nullptr, kFlags)) {
      KeepOnTop();
      Context context {.world = world, .console = console};
      if (touch_screen) {
        DrawKeyboardButtons(context);
      }
      DrawTabs(context);
    }
    ImGui::End();
  }
  if (touch_screen) {
    DrawToggleButton(console, touch_screen);
  } else {
    console.button_area.reset();
  }
}

}  // namespace z13::raylib::gui
