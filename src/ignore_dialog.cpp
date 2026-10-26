#include "ignore_dialog.h"

#include <commctrl.h>
#include <shellapi.h>
#include <spdlog/spdlog.h>
#include <uxtheme.h>
#include <windowsx.h>

#include <algorithm>
#include <cstring>
#include <map>

#include "resource.h"
#include "ui_theme.h"

#pragma comment(lib, "UxTheme.lib")

#ifndef DOCTEST_CONFIG_DISABLE
#include <doctest/doctest.h>
#endif

namespace wintiler {
std::vector<IgnoreRuleEntry> make_ignore_rule_entries(const std::vector<IgnoreRule>& user,
                                                      const IgnoreOptions& options) {
  std::vector<IgnoreRuleEntry> result;
  for (const auto& rule : user)
    result.push_back({rule, false});
  const auto defaults = get_default_ignore_options();
  if (options.merge_processes)
    for (const auto& value : defaults.ignored_processes)
      result.push_back({{IgnoreRuleKind::Process, value, {}}, true});
  if (options.merge_window_titles)
    for (const auto& value : defaults.ignored_window_titles)
      result.push_back({{IgnoreRuleKind::Title, {}, value}, true});
  if (options.merge_process_title_pairs)
    for (const auto& [process, title] : defaults.ignored_process_title_pairs)
      result.push_back({{IgnoreRuleKind::ProcessTitlePair, process, title}, true});
  if (options.merge_ignore_children_of_processes)
    for (const auto& value : defaults.ignore_children_of_processes)
      result.push_back({{IgnoreRuleKind::ChildProcess, value, {}}, true});
  return result;
}

bool rule_is_related_to_window(const IgnoreRule& rule,
                               const winapi::WindowManagementSnapshot& window) {
  return ignore_rule_matches(rule, window.process_name, window.title, window.is_child_or_owned) ||
         (rule.kind == IgnoreRuleKind::ChildProcess &&
          winapi::matches_ignored_process_name(window.process_name, rule.process));
}

namespace {
constexpr wchar_t dialog_class[] = L"WinTilerIgnoreDialog";
constexpr wchar_t editor_class[] = L"WinTilerIgnoreRuleEditor";
constexpr UINT open_row_message = WM_APP + 61;
constexpr int id_list = 2100, id_search = 2101, id_windows = 2102, id_rules = 2103;
constexpr int id_all = 2104, id_tiled = 2105, id_ignored = 2106, id_refresh = 2107;
constexpr int id_config = 2108, id_undo = 2109, id_create = 2110, id_retry = 2111;
HWND dialog_window = nullptr;

struct Style {
  UINT dpi = 96;
  ui::ThemeMode mode = ui::ThemeMode::Light;
  ui::Palette colors = ui::palette_for(mode);
  HFONT body = nullptr, caption_font = nullptr, title = nullptr, semibold = nullptr;
  HBRUSH background = nullptr, surface = nullptr;
  ~Style() {
    release();
  }
  void release() {
    for (HGDIOBJ object : {static_cast<HGDIOBJ>(body), static_cast<HGDIOBJ>(caption_font),
                           static_cast<HGDIOBJ>(title), static_cast<HGDIOBJ>(semibold),
                           static_cast<HGDIOBJ>(background), static_cast<HGDIOBJ>(surface)}) {
      if (object && !DeleteObject(object))
        spdlog::error("Failed to release dialog drawing resource: {}", GetLastError());
    }
    body = caption_font = title = semibold = nullptr;
    background = surface = nullptr;
  }
  int s(int value) const {
    return ui::scale(value, dpi);
  }
  void update(HWND window) {
    release();
    dpi = GetDpiForWindow(window);
    if (!dpi)
      dpi = 96;
    mode = ui::system_theme();
    colors = ui::palette_for(mode);
    body = ui::make_font(13, dpi);
    caption_font = ui::make_font(11, dpi);
    title = ui::make_font(24, dpi, FW_SEMIBOLD);
    semibold = ui::make_font(13, dpi, FW_SEMIBOLD);
    background = CreateSolidBrush(colors.background);
    surface = CreateSolidBrush(colors.surface);
    if (!background || !surface)
      spdlog::error("Failed to create dialog brushes: {}", GetLastError());
    ui::apply_titlebar(window, mode);
  }
};

struct Row {
  std::wstring name, detail, behavior, scope;
  std::string identity;
  std::optional<winapi::WindowManagementSnapshot> window;
  std::optional<IgnoreRuleEntry> entry;
  bool ignored = false;
};

struct PendingEdit {
  std::vector<IgnoreRule> remove, add;
  std::optional<winapi::WindowManagementSnapshot> window;
  bool undo = false;
};

struct DialogState {
  HWND hwnd = nullptr, list = nullptr, search = nullptr;
  std::map<int, HWND> buttons;
  Style style;
  std::optional<std::filesystem::path> path;
  IgnoreOptions options;
  std::function<void(const IgnoreOptions&)> changed;
  std::vector<winapi::WindowManagementSnapshot> windows;
  std::vector<IgnoreRuleEntry> rules;
  std::vector<Row> rows;
  std::optional<IgnoreRuleEdit> undo;
  std::optional<PendingEdit> pending;
  bool saved_view = false, loading = false, readable = false, exists = false;
  int filter = 0;
  std::wstring query,
      message = L"Choose a row's menu to create or change a rule. Changes save immediately.";
  bool error = false;
};

std::wstring window_text(HWND control) {
  int length = GetWindowTextLengthW(control);
  std::wstring result(static_cast<size_t>(length) + 1, L'\0');
  int copied = GetWindowTextW(control, result.data(), static_cast<int>(result.size()));
  result.resize(static_cast<size_t>((std::max)(0, copied)));
  return result;
}

std::string rule_identity(const IgnoreRuleEntry& entry) {
  return std::to_string(static_cast<int>(entry.rule.kind)) + ":" +
         (entry.built_in ? "builtin:" : "user:") + entry.rule.process + "\n" + entry.rule.title;
}

bool contains_query(std::wstring_view value, std::wstring_view query) {
  if (query.empty())
    return true;
  return FindNLSStringEx(LOCALE_NAME_INVARIANT, FIND_FROMSTART | NORM_IGNORECASE, value.data(),
                         static_cast<int>(value.size()), query.data(),
                         static_cast<int>(query.size()), nullptr, nullptr, nullptr, 0) >= 0;
}

std::wstring rule_value(const IgnoreRule& rule) {
  if (rule.kind == IgnoreRuleKind::Title)
    return ui::wide(rule.title);
  if (rule.kind == IgnoreRuleKind::ProcessTitlePair)
    return ui::wide(rule.process + "  |  " + rule.title);
  return ui::wide(rule.process);
}

size_t match_count(const DialogState& state, const IgnoreRule& rule) {
  return static_cast<size_t>(
      std::count_if(state.windows.begin(), state.windows.end(), [&](const auto& w) {
        return ignore_rule_matches(rule, w.process_name, w.title, w.is_child_or_owned);
      }));
}

std::optional<Row> selected_row(const DialogState& state) {
  LRESULT index = SendMessageW(state.list, LB_GETCURSEL, 0, 0);
  if (index == LB_ERR || static_cast<size_t>(index) >= state.rows.size())
    return std::nullopt;
  return state.rows[static_cast<size_t>(index)];
}

void layout(DialogState& state);
void rebuild_rows(DialogState& state);

void feedback(DialogState& state, std::wstring message, bool error = false) {
  state.message = std::move(message);
  state.error = error;
  EnableWindow(state.buttons[id_undo], state.undo.has_value() && state.readable);
  ShowWindow(state.buttons[id_undo], state.undo.has_value() ? SW_SHOW : SW_HIDE);
  ShowWindow(state.buttons[id_retry], state.pending.has_value() ? SW_SHOW : SW_HIDE);
  InvalidateRect(state.hwnd, nullptr, FALSE);
  // Expose feedback to accessibility clients through a native static control.
  if (HWND status = GetDlgItem(state.hwnd, 2112))
    SetWindowTextW(status, state.message.c_str());
}

void refresh(DialogState& state) {
  std::vector<IgnoreRule> user;
  state.readable = false;
  state.exists = false;
  if (state.path) {
    std::error_code error;
    state.exists = std::filesystem::exists(*state.path, error);
    if (state.exists) {
      auto rules = read_user_ignore_rules(*state.path);
      auto loaded = read_options_toml(*state.path);
      if (rules && loaded) {
        user = std::move(*rules);
        state.options = loaded->ignoreOptions;
        state.readable = true;
      } else {
        feedback(state, ui::wide(rules ? loaded.error() : rules.error()), true);
      }
    }
  }
  state.rules = make_ignore_rule_entries(user, state.options);
  state.windows.clear();
  for (auto& window : winapi::gather_window_management_snapshots(state.options)) {
    if (winapi::should_show_window_management_snapshot_in_ignore_dialog(window))
      state.windows.push_back(std::move(window));
  }
  std::stable_sort(state.windows.begin(), state.windows.end(), [](const auto& a, const auto& b) {
    return std::tie(a.process_name, a.title) < std::tie(b.process_name, b.title);
  });
  ShowWindow(state.buttons[id_create], state.path && !state.exists ? SW_SHOW : SW_HIDE);
  EnableWindow(state.buttons[id_config], state.path && state.exists);
  EnableWindow(state.buttons[id_undo], state.readable && state.undo.has_value());
  rebuild_rows(state);
  layout(state);
}

void rebuild_rows(DialogState& state) {
  auto selection = selected_row(state);
  std::string selected = selection ? selection->identity : "";
  state.rows.clear();
  auto add = [&](Row row) {
    if (!contains_query(row.name + L" " + row.detail + L" " + row.scope, state.query))
      return;
    bool category = state.saved_view ? row.entry->built_in : row.ignored;
    if ((state.filter == 1 && category) || (state.filter == 2 && !category))
      return;
    state.rows.push_back(std::move(row));
  };
  if (state.saved_view) {
    for (const auto& entry : state.rules) {
      Row row;
      row.entry = entry;
      row.identity = rule_identity(entry);
      row.name = ui::wide(entry.rule.process.empty() ? entry.rule.title : entry.rule.process);
      row.detail = entry.rule.kind == IgnoreRuleKind::ProcessTitlePair ? ui::wide(entry.rule.title)
                   : entry.rule.kind == IgnoreRuleKind::Title ? L"Any app with this exact title"
                   : entry.rule.kind == IgnoreRuleKind::ChildProcess
                       ? L"Child and owned windows from this app"
                       : L"All windows from this app";
      row.behavior = ui::wide(ignore_rule_kind_name(entry.rule.kind));
      row.scope = std::to_wstring(match_count(state, entry.rule)) + L" listed  |  " +
                  (entry.built_in ? L"Built-in" : L"Your rule");
      add(std::move(row));
    }
  } else {
    for (const auto& window : state.windows) {
      Row row;
      row.window = window;
      row.name = ui::wide(window.title);
      row.detail = ui::wide(window.process_name);
      row.identity = std::to_string(reinterpret_cast<uintptr_t>(window.handle)) + ":" +
                     std::to_string(window.pid.value_or(0));
      row.ignored = !window.is_currently_managed;
      row.behavior = row.ignored ? L"Ignored" : L"Tiled";
      std::vector<const IgnoreRuleEntry*> matched;
      bool owned_rule = false;
      for (const auto& entry : state.rules) {
        if (ignore_rule_matches(entry.rule, window.process_name, window.title,
                                window.is_child_or_owned))
          matched.push_back(&entry);
        if (entry.rule.kind == IgnoreRuleKind::ChildProcess &&
            rule_is_related_to_window(entry.rule, window))
          owned_rule = true;
      }
      if (matched.size() > 1)
        row.scope = std::to_wstring(matched.size()) + L" matching rules";
      else if (!matched.empty())
        row.scope = ui::wide(ignore_rule_kind_name(matched[0]->rule.kind)) +
                    (matched[0]->built_in ? L"  |  Built-in" : L"");
      else if (window.is_below_small_window_barrier)
        row.scope = L"Small-window limit";
      else if (row.ignored)
        row.scope = L"Global / system filter";
      else
        row.scope = owned_rule ? L"Owned windows ignored" : L"No ignore rule";
      add(std::move(row));
    }
  }
  state.loading = true;
  SendMessageW(state.list, WM_SETREDRAW, FALSE, 0);
  SendMessageW(state.list, LB_RESETCONTENT, 0, 0);
  int selected_index = -1;
  for (size_t i = 0; i < state.rows.size(); ++i) {
    const auto& row = state.rows[i];
    std::wstring accessible = row.name + L", " + row.detail + L", " + row.behavior + L", " +
                              row.scope + L". Press Enter for rule actions.";
    if (SendMessageW(state.list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(accessible.c_str())) ==
        LB_ERR)
      spdlog::error("Failed to add ignore dialog row");
    if (row.identity == selected)
      selected_index = static_cast<int>(i);
  }
  if (!state.rows.empty())
    SendMessageW(state.list, LB_SETCURSEL, selected_index >= 0 ? selected_index : 0, 0);
  SendMessageW(state.list, WM_SETREDRAW, TRUE, 0);
  InvalidateRect(state.list, nullptr, TRUE);
  state.loading = false;
  SetWindowTextW(state.buttons[id_windows],
                 (L"Open windows  " + std::to_wstring(state.windows.size())).c_str());
  SetWindowTextW(state.buttons[id_rules],
                 (L"Saved rules  " + std::to_wstring(state.rules.size())).c_str());
  SetWindowTextW(state.buttons[id_tiled], state.saved_view ? L"Yours" : L"Tiled");
  SetWindowTextW(state.buttons[id_ignored], state.saved_view ? L"Built-in" : L"Ignored");
  SendMessageW(state.search, EM_SETCUEBANNER, FALSE,
               reinterpret_cast<LPARAM>(state.saved_view ? L"Search saved rules"
                                                         : L"Search apps or window titles"));
  InvalidateRect(state.hwnd, nullptr, FALSE);
}

bool open_config(DialogState& state) {
  if (!state.path || !state.exists)
    return false;
  HINSTANCE result =
      ShellExecuteW(state.hwnd, L"open", state.path->c_str(), nullptr, nullptr, SW_SHOWNORMAL);
  if (reinterpret_cast<INT_PTR>(result) <= 32) {
    feedback(state, L"Could not open the config file in its editor.", true);
    return false;
  }
  return true;
}

bool valid_window(const winapi::WindowManagementSnapshot& window) {
  if (!winapi::is_window_valid(window.handle))
    return false;
  auto current = winapi::get_window_info(window.handle);
  return current.pid == window.pid && current.processName == window.process_name &&
         current.title == window.title && current.className == window.class_name;
}

void apply_edit(DialogState& state, PendingEdit edit) {
  if (!state.path)
    return;
  if (edit.window && !valid_window(*edit.window)) {
    state.pending.reset();
    refresh(state);
    feedback(state, L"That window changed or closed. Choose its current row and try again.", true);
    return;
  }
  auto result = edit_ignore_rules(*state.path, edit.remove, edit.add, !state.exists && !edit.undo);
  if (!result) {
    spdlog::error("Ignore rule edit failed: {}", result.error());
    state.pending = edit;
    feedback(state, ui::wide(result.error()), true);
    return;
  }
  state.pending.reset();
  if (!result->changed()) {
    feedback(state, L"That rule is already active. Nothing changed.");
    return;
  }
  if (edit.undo)
    state.undo.reset();
  else
    state.undo = *result;
  refresh(state);
  state.changed(state.options);
  feedback(state,
           edit.undo ? L"Last rule change undone. Other config edits were preserved."
           : state.readable
               ? L"Rule saved. All matching windows will update; other rules still apply."
               : L"Rule saved, but the config could not be reloaded. Check the file and refresh.",
           !state.readable);
  SetFocus(state.list);
}

void copy_details(DialogState& state, const Row& row) {
  std::wstring detail =
      row.name + L"\r\n" + row.detail + L"\r\n" + row.behavior + L" - " + row.scope;
  if (row.window) {
    detail += L"\r\nClass: " + ui::wide(row.window->class_name) + L"\r\nReason: " +
              ui::wide(row.window->ignore_reason);
    detail += L"\r\nPID: " + std::to_wstring(row.window->pid.value_or(0));
    if (row.window->monitor_index)
      detail += L"\r\nDisplay: " + std::to_wstring(*row.window->monitor_index + 1);
    if (row.window->rect)
      detail += L"\r\nSize: " + std::to_wstring(row.window->rect->right - row.window->rect->left) +
                L" x " + std::to_wstring(row.window->rect->bottom - row.window->rect->top);
  }
  HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (detail.size() + 1) * sizeof(wchar_t));
  if (!memory) {
    feedback(state, L"Could not allocate clipboard memory.", true);
    return;
  }
  auto* destination = GlobalLock(memory);
  bool copied = false;
  if (destination) {
    std::memcpy(destination, detail.c_str(), (detail.size() + 1) * sizeof(wchar_t));
    SetLastError(NO_ERROR);
    if (!GlobalUnlock(memory) && GetLastError() != NO_ERROR)
      spdlog::error("Clipboard memory unlock failed: {}", GetLastError());
    if (OpenClipboard(state.hwnd)) {
      copied = EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
      if (!CloseClipboard())
        spdlog::error("Could not close clipboard: {}", GetLastError());
    }
  }
  if (!copied && GlobalFree(memory))
    spdlog::error("Could not release clipboard memory: {}", GetLastError());
  feedback(state, copied ? L"Details copied." : L"Could not copy details.", !copied);
}

// The small value editor is only used for literal text changes or conversions
// that need an app/title which a saved rule does not contain.
struct EditorState {
  HWND hwnd = nullptr, process = nullptr, title = nullptr;
  Style style;
  IgnoreRule value;
  std::optional<IgnoreRule> result;
  std::wstring error;
};

void draw_button(const DRAWITEMSTRUCT& item, const Style& style, bool active = false,
                 bool ghost = false, bool primary = false) {
  auto colors = style.colors;
  bool disabled = (item.itemState & ODS_DISABLED) != 0;
  bool selected = active || (item.itemState & ODS_SELECTED) != 0;
  primary = primary && !disabled;
  ui::fill(item.hDC, item.rcItem, colors.background);
  RECT rect = item.rcItem;
  InflateRect(&rect, -1, -1);
  ui::rounded(item.hDC, rect,
              primary    ? colors.accent
              : selected ? colors.selection
              : ghost    ? colors.background
                         : colors.surface,
              primary    ? colors.accent
              : selected ? colors.selection
              : ghost    ? colors.background
                         : colors.border,
              style.s(8));
  ui::text(item.hDC, rect, window_text(item.hwndItem), style.body,
           disabled   ? colors.muted
           : primary  ? colors.accent_text
           : selected ? colors.selection_text
                      : colors.foreground,
           DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
  if (item.itemState & ODS_FOCUS) {
    InflateRect(&rect, -style.s(3), -style.s(3));
    DrawFocusRect(item.hDC, &rect);
  }
}

LRESULT CALLBACK editor_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  auto* state = reinterpret_cast<EditorState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    state = static_cast<EditorState*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
    state->hwnd = hwnd;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
  }
  if (!state)
    return DefWindowProcW(hwnd, message, wparam, lparam);
  const bool uses_process = state->value.kind != IgnoreRuleKind::Title;
  const bool uses_title = state->value.kind == IgnoreRuleKind::Title ||
                          state->value.kind == IgnoreRuleKind::ProcessTitlePair;
  const int title_y = uses_process ? 140 : 76;
  auto arrange = [&] {
    const auto& st = state->style;
    RECT area{};
    GetClientRect(hwnd, &area);
    MoveWindow(state->process, st.s(34), st.s(81), area.right - st.s(68), st.s(21), TRUE);
    MoveWindow(state->title, st.s(34), st.s(title_y + 5), area.right - st.s(68), st.s(21), TRUE);
    MoveWindow(GetDlgItem(hwnd, IDCANCEL), area.right - st.s(210), area.bottom - st.s(48), st.s(84),
               st.s(30), TRUE);
    MoveWindow(GetDlgItem(hwnd, IDOK), area.right - st.s(116), area.bottom - st.s(48), st.s(92),
               st.s(30), TRUE);
  };
  switch (message) {
  case WM_CREATE: {
    state->style.update(hwnd);
    state->process = CreateWindowExW(0, L"EDIT", ui::wide(state->value.process).c_str(),
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0,
                                     0, hwnd, reinterpret_cast<HMENU>(101), nullptr, nullptr);
    state->title = CreateWindowExW(0, L"EDIT", ui::wide(state->value.title).c_str(),
                                   WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0,
                                   hwnd, reinterpret_cast<HMENU>(102), nullptr, nullptr);
    for (auto [id, label] : {std::pair{IDCANCEL, L"Cancel"}, std::pair{IDOK, L"Save values"}}) {
      if (!CreateWindowExW(0, L"BUTTON", label, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                           0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                           nullptr, nullptr))
        spdlog::error("Failed to create value editor button: {}", GetLastError());
    }
    if (!state->process || !state->title)
      return -1;
    for (HWND edit : {state->process, state->title})
      SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(state->style.body), TRUE);
    ShowWindow(state->process, uses_process ? SW_SHOW : SW_HIDE);
    ShowWindow(state->title, uses_title ? SW_SHOW : SW_HIDE);
    arrange();
    return 0;
  }
  case WM_SIZE:
    arrange();
    return 0;
  case WM_DPICHANGED: {
    auto* rect = reinterpret_cast<RECT*>(lparam);
    SetWindowPos(hwnd, nullptr, rect->left, rect->top, rect->right - rect->left,
                 rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
    [[fallthrough]];
  }
  case WM_THEMECHANGED:
  case WM_SETTINGCHANGE:
  case WM_SYSCOLORCHANGE:
    state->style.update(hwnd);
    for (HWND edit : {state->process, state->title})
      SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(state->style.body), TRUE);
    arrange();
    InvalidateRect(hwnd, nullptr, TRUE);
    return 0;
  case WM_PAINT: {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(hwnd, &paint);
    RECT area{};
    GetClientRect(hwnd, &area);
    const auto& st = state->style;
    ui::fill(dc, area, st.colors.background);
    ui::text(dc, {st.s(24), st.s(16), area.right - st.s(24), st.s(42)}, L"Edit matching values",
             st.semibold, st.colors.foreground);
    auto field = [&](int y, const wchar_t* label, HWND control) {
      ui::text(dc, {st.s(24), st.s(y - 24), area.right - st.s(24), st.s(y - 2)}, label, st.body,
               st.colors.muted);
      ui::rounded(dc, {st.s(24), st.s(y), area.right - st.s(24), st.s(y + 30)}, st.colors.surface,
                  GetFocus() == control ? st.colors.accent_label : st.colors.border, st.s(8));
    };
    if (uses_process)
      field(76, L"App executable", state->process);
    if (uses_title)
      field(title_y, L"Exact window title", state->title);
    ui::text(dc,
             {st.s(24), st.s(uses_process && uses_title ? 180 : 116), area.right - st.s(24),
              area.bottom - st.s(58)},
             state->error.empty() ? L"The change applies to every matching window." : state->error,
             st.caption_font, state->error.empty() ? st.colors.muted : st.colors.error,
             DT_WORDBREAK | DT_NOPREFIX);
    EndPaint(hwnd, &paint);
    return 0;
  }
  case WM_CTLCOLOREDIT:
  case WM_CTLCOLORSTATIC:
    SetBkColor(reinterpret_cast<HDC>(wparam), state->style.colors.surface);
    SetTextColor(reinterpret_cast<HDC>(wparam), state->style.colors.foreground);
    return reinterpret_cast<LRESULT>(state->style.surface);
  case WM_DRAWITEM:
    draw_button(*reinterpret_cast<DRAWITEMSTRUCT*>(lparam), state->style, false, false,
                LOWORD(wparam) == IDOK);
    return TRUE;
  case DM_GETDEFID:
    return MAKELRESULT(IDOK, DC_HASDEFID);
  case WM_COMMAND:
    if (HIWORD(wparam) == EN_SETFOCUS || HIWORD(wparam) == EN_KILLFOCUS)
      InvalidateRect(hwnd, nullptr, FALSE);
    if (LOWORD(wparam) == IDOK) {
      state->value.process = ui::utf8(window_text(state->process));
      state->value.title = ui::utf8(window_text(state->title));
      if (!ignore_rule_has_values(state->value)) {
        state->error = L"Enter the values required by this rule.";
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      }
      state->result = state->value;
      DestroyWindow(hwnd);
      return 0;
    }
    if (LOWORD(wparam) == IDCANCEL) {
      DestroyWindow(hwnd);
      return 0;
    }
    break;
  case WM_CLOSE:
    DestroyWindow(hwnd);
    return 0;
  case WM_ERASEBKGND:
    return 1;
  default:
    break;
  }
  return DefWindowProcW(hwnd, message, wparam, lparam);
}

std::optional<IgnoreRule> edit_values(HWND owner, IgnoreRule rule) {
  WNDCLASSEXW klass{sizeof(WNDCLASSEXW)};
  klass.lpfnWndProc = editor_proc;
  klass.hInstance = GetModuleHandleW(nullptr);
  klass.lpszClassName = editor_class;
  klass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  if (!RegisterClassExW(&klass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    spdlog::error("Could not register value editor: {}", GetLastError());
    return std::nullopt;
  }
  EditorState state;
  state.value = std::move(rule);
  UINT dpi = GetDpiForWindow(owner);
  RECT parent{};
  GetWindowRect(owner, &parent);
  int width = ui::scale(470, dpi);
  int height = ui::scale(state.value.kind == IgnoreRuleKind::ProcessTitlePair ? 310 : 246, dpi);
  HWND hwnd =
      CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT, editor_class,
                      L"Edit matching values", WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_CLIPCHILDREN,
                      parent.left + (parent.right - parent.left - width) / 2,
                      parent.top + (parent.bottom - parent.top - height) / 2, width, height, owner,
                      nullptr, GetModuleHandleW(nullptr), &state);
  if (!hwnd) {
    spdlog::error("Could not open value editor: {}", GetLastError());
    return std::nullopt;
  }
  EnableWindow(owner, FALSE);
  ShowWindow(hwnd, SW_SHOW);
  SetFocus(state.value.kind == IgnoreRuleKind::Title ? state.title : state.process);
  MSG msg{};
  while (IsWindow(hwnd)) {
    int status = GetMessageW(&msg, nullptr, 0, 0);
    if (status <= 0) {
      if (status == 0)
        PostQuitMessage(static_cast<int>(msg.wParam));
      break;
    }
    if (!IsDialogMessageW(hwnd, &msg)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }
  if (IsWindow(hwnd))
    DestroyWindow(hwnd);
  EnableWindow(owner, TRUE);
  if (!SetForegroundWindow(owner))
    spdlog::debug("Could not restore dialog foreground after editing");
  return state.result;
}

enum class MenuActionKind { Change, EditValues, Details, Copy };
struct MenuAction {
  MenuActionKind kind = MenuActionKind::Change;
  std::optional<IgnoreRule> before, after;
};

void populate_row_menu(const DialogState& state, const Row& row, ui::PopupMenu& menu,
                       std::vector<MenuAction>& actions) {
  auto add_action = [&](HMENU parent, std::wstring label, std::wstring description,
                        MenuAction action, bool enabled, bool checked = false) {
    actions.push_back(std::move(action));
    menu.append(4000 + static_cast<UINT>(actions.size() - 1), std::move(label),
                std::move(description), enabled, checked, parent);
  };
  auto add_states = [&](HMENU parent, std::optional<IgnoreRuleEntry> current, bool multiple) {
    bool locked = current && current->built_in;
    bool can_edit = (state.readable || (state.path && !state.exists)) && !locked;
    std::wstring heading = locked ? L"Built-in rule" : current ? L"Change rule" : L"Assign a rule";
    menu.append(0, heading, current ? rule_value(current->rule) : row.detail, false, false, parent);
    if (!can_edit && !locked)
      menu.append(0, L"Rule editing unavailable",
                  state.path ? L"Check the config error, then refresh"
                             : L"No config path is available",
                  false, false, parent);
    menu.separator(parent);
    bool global_filter = row.window && row.ignored && !current;
    std::wstring remove_label = state.saved_view ? L"Remove rule"
                                : multiple       ? L"Remove this rule"
                                : global_filter  ? L"Ignored by global / system settings"
                                                 : L"Tile normally";
    std::wstring remove_description = current ? std::to_wstring(match_count(state, current->rule)) +
                                                    L" listed window(s); other rules still apply"
                                      : global_filter ? L"Open config to adjust the global filter"
                                                      : L"No saved exclusion for this window";
    add_action(parent, remove_label, remove_description,
               {MenuActionKind::Change,
                current ? std::optional<IgnoreRule>(current->rule) : std::nullopt, std::nullopt},
               can_edit && current.has_value() && !global_filter, !current && !row.ignored);
    menu.separator(parent);
    constexpr std::array kinds{IgnoreRuleKind::ProcessTitlePair, IgnoreRuleKind::Process,
                               IgnoreRuleKind::Title, IgnoreRuleKind::ChildProcess};
    for (auto kind : kinds) {
      IgnoreRule target{kind, {}, {}};
      if (row.window) {
        target.process = row.window->process_name;
        target.title = row.window->title;
      } else if (current) {
        target.process = current->rule.process;
        target.title = current->rule.title;
      }
      std::wstring label, description;
      switch (kind) {
      case IgnoreRuleKind::ProcessTitlePair:
        label = L"Ignore app + exact title";
        description = L"Only this app with this title";
        break;
      case IgnoreRuleKind::Process:
        label = L"Ignore the entire app";
        description = L"All windows from this app, now and later";
        break;
      case IgnoreRuleKind::Title:
        label = L"Ignore this title everywhere";
        description = L"This exact title, including other apps";
        break;
      case IgnoreRuleKind::ChildProcess:
        label = L"Ignore owned windows";
        description = L"Only child / owned windows from this app";
        break;
      }
      if (ignore_rule_has_values(target))
        description = std::to_wstring(match_count(state, target)) + L" listed  |  " + description;
      else
        description = L"Enter the missing app name or title...";
      add_action(parent, label, description,
                 {MenuActionKind::Change,
                  current ? std::optional<IgnoreRule>(current->rule) : std::nullopt, target},
                 can_edit, current && current->rule.kind == kind);
    }
    if (current && !locked) {
      menu.separator(parent);
      add_action(parent, L"Edit matching values...", L"Change the saved app name or title",
                 {MenuActionKind::EditValues, current->rule, current->rule}, can_edit);
    }
    if (locked)
      menu.append(0, L"Built-in defaults stay active", L"Manage default merging in the config file",
                  false, false, parent);
  };
  std::vector<IgnoreRuleEntry> related;
  if (row.entry)
    related.push_back(*row.entry);
  else
    for (const auto& entry : state.rules)
      if (rule_is_related_to_window(entry.rule, *row.window))
        related.push_back(entry);
  if (related.size() <= 1)
    add_states(menu.handle, related.empty() ? std::nullopt : std::optional(related.front()), false);
  else {
    menu.append(0, L"Choose the rule to change", L"Other matching rules will remain active", false);
    for (const auto& entry : related) {
      HMENU child =
          menu.submenu(ui::wide(ignore_rule_kind_name(entry.rule.kind)) +
                       (entry.built_in ? L" (built-in) - " : L" - ") + rule_value(entry.rule));
      if (child)
        add_states(child, entry, true);
    }
    HMENU child = menu.submenu(L"Create another rule");
    if (child)
      add_states(child, std::nullopt, true);
  }
  menu.separator();
  if (row.window)
    add_action(menu.handle, L"Window details...", L"Class, display, size, and exclusion reason",
               {MenuActionKind::Details, {}, {}}, true);
  add_action(menu.handle, L"Copy details", {}, {MenuActionKind::Copy, {}, {}}, true);
}

void show_row_menu(DialogState& state, int index, std::optional<POINT> location = std::nullopt) {
  if (index < 0 || static_cast<size_t>(index) >= state.rows.size())
    return;
  SendMessageW(state.list, LB_SETCURSEL, index, 0);
  Row old_row = state.rows[static_cast<size_t>(index)];
  // Re-read the file and window metadata before offering actions. HWND alone is
  // not an identity; it may have been recycled by the time the menu is invoked.
  refresh(state);
  auto found = std::find_if(state.rows.begin(), state.rows.end(),
                            [&](const auto& row) { return row.identity == old_row.identity; });
  if (found == state.rows.end()) {
    feedback(state, L"That item changed or closed. Choose its current row.", true);
    return;
  }
  Row row = *found;
  if (row.window && (row.window->title != old_row.window->title || !valid_window(*row.window))) {
    feedback(state, L"The window title changed. The list has been refreshed.", true);
    return;
  }
  index = static_cast<int>(std::distance(state.rows.begin(), found));
  SendMessageW(state.list, LB_SETCURSEL, index, 0);
  POINT point{};
  if (location)
    point = *location;
  else {
    RECT rect{};
    SendMessageW(state.list, LB_GETITEMRECT, index, reinterpret_cast<LPARAM>(&rect));
    point = {rect.right - state.style.s(340), rect.bottom};
    if (!ClientToScreen(state.list, &point))
      spdlog::error("Could not position item menu: {}", GetLastError());
  }
  ui::PopupMenu menu(state.hwnd);
  std::vector<MenuAction> actions;
  populate_row_menu(state, row, menu, actions);
  HWND owner = state.hwnd;
  UINT command = menu.track(point);
  if (!IsWindow(owner) || command < 4000 || command - 4000 >= actions.size())
    return;
  MenuAction action = actions[command - 4000];
  switch (action.kind) {
  case MenuActionKind::Copy:
    copy_details(state, row);
    return;
  case MenuActionKind::Details: {
    std::wstring details = row.name + L"\n" + row.detail + L"\n\n" + row.behavior + L" - " +
                           row.scope + L"\nClass: " + ui::wide(row.window->class_name) +
                           L"\nReason: " + ui::wide(row.window->ignore_reason);
    if (row.window->monitor_index)
      details += L"\nDisplay: " + std::to_wstring(*row.window->monitor_index + 1);
    if (row.window->rect)
      details += L"\nSize: " + std::to_wstring(row.window->rect->right - row.window->rect->left) +
                 L" x " + std::to_wstring(row.window->rect->bottom - row.window->rect->top);
    MessageBoxW(owner, details.c_str(), L"Window details", MB_OK | MB_ICONINFORMATION);
    return;
  }
  case MenuActionKind::EditValues:
    action.after = edit_values(owner, *action.after);
    if (!action.after || !IsWindow(owner))
      return;
    break;
  case MenuActionKind::Change:
    if (action.after && !ignore_rule_has_values(*action.after)) {
      action.after = edit_values(owner, *action.after);
      if (!action.after || !IsWindow(owner))
        return;
    }
    break;
  }
  PendingEdit edit;
  if (action.before)
    edit.remove.push_back(*action.before);
  if (action.after)
    edit.add.push_back(*action.after);
  // Editing existing literal values is config-based; state assignments derived
  // from a live row must still refer to the same process and window title.
  if (action.kind == MenuActionKind::Change && action.after)
    edit.window = row.window;
  apply_edit(state, std::move(edit));
}

void layout(DialogState& state) {
  RECT client{};
  GetClientRect(state.hwnd, &client);
  if (client.right == 0 || client.bottom == 0)
    return;
  const auto& st = state.style;
  int margin = st.s(24), offset = state.path && !state.exists ? st.s(56) : 0;
  auto move = [&](HWND child, int x, int y, int width, int height) {
    if (child && !MoveWindow(child, x, y, (std::max)(width, 1), (std::max)(height, 1), TRUE))
      spdlog::debug("Could not place dialog control: {}", GetLastError());
  };
  move(state.buttons[id_windows], margin, st.s(102), st.s(155), st.s(32));
  move(state.buttons[id_rules], margin + st.s(163), st.s(102), st.s(142), st.s(32));
  move(state.buttons[id_create], client.right - margin - st.s(160), st.s(150), st.s(160), st.s(32));
  int search_width = client.right - 2 * margin - st.s(278);
  move(state.search, margin + st.s(12), st.s(157) + offset, search_width - st.s(24), st.s(24));
  int x = margin + search_width + st.s(10);
  for (auto [id, width] : {std::pair{id_all, 48}, std::pair{id_tiled, 62},
                           std::pair{id_ignored, 76}, std::pair{id_refresh, 72}}) {
    move(state.buttons[id], x, st.s(150) + offset, st.s(width), st.s(34));
    x += st.s(width + 3);
  }
  move(state.list, margin, st.s(218) + offset, client.right - margin * 2,
       client.bottom - st.s(318) - offset);
  SendMessageW(state.list, LB_SETITEMHEIGHT, 0, st.s(64));
  int footer = client.bottom - st.s(84);
  move(state.buttons[id_config], margin, footer, st.s(180), st.s(32));
  move(state.buttons[id_undo], margin + st.s(190), footer, st.s(64), st.s(32));
  move(state.buttons[id_retry], margin + st.s(264), footer, st.s(64), st.s(32));
  move(state.buttons[IDCANCEL], client.right - margin - st.s(80), footer, st.s(80), st.s(32));
  move(GetDlgItem(state.hwnd, 2112), margin, client.bottom - st.s(43), client.right - margin * 2,
       st.s(34));
  InvalidateRect(state.hwnd, nullptr, FALSE);
}

void update_style(DialogState& state) {
  state.style.update(state.hwnd);
  for (HWND child = GetWindow(state.hwnd, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
    SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(state.style.body), TRUE);
  SendMessageW(GetDlgItem(state.hwnd, 2112), WM_SETFONT,
               reinterpret_cast<WPARAM>(state.style.caption_font), TRUE);
  const wchar_t* theme =
      state.style.mode == ui::ThemeMode::Dark ? L"DarkMode_Explorer" : L"Explorer";
  for (HWND control : {state.list, state.search}) {
    HRESULT result = SetWindowTheme(
        control, state.style.mode == ui::ThemeMode::HighContrast ? nullptr : theme, nullptr);
    if (FAILED(result))
      spdlog::debug("Control theme unavailable: {}", static_cast<long>(result));
  }
  layout(state);
  RedrawWindow(state.hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE);
}

void draw_row(DialogState& state, const DRAWITEMSTRUCT& item) {
  const auto& st = state.style;
  const auto& c = st.colors;
  ui::fill(item.hDC, item.rcItem, c.background);
  if (item.itemID == static_cast<UINT>(-1) || item.itemID >= state.rows.size())
    return;
  const auto& row = state.rows[item.itemID];
  bool selected = (item.itemState & ODS_SELECTED) != 0;
  RECT rect = item.rcItem;
  InflateRect(&rect, -1, -st.s(2));
  if (selected)
    ui::rounded(item.hDC, rect, c.selection, c.selection, st.s(10));
  COLORREF foreground = selected ? c.selection_text : c.foreground;
  RECT symbol{rect.left + st.s(10), rect.top + st.s(14), rect.left + st.s(42), rect.top + st.s(46)};
  ui::rounded(item.hDC, symbol, c.surface, c.border, st.s(7));
  if (state.saved_view) {
    ui::text(item.hDC, symbol, L"\x2261", st.body, c.muted, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  } else {
    RECT icon = symbol;
    InflateRect(&icon, -st.s(8), -st.s(10));
    ui::rounded(item.hDC, icon, c.surface, c.muted, st.s(2));
    ui::fill(item.hDC, {icon.left, icon.top + st.s(4), icon.right, icon.top + st.s(4) + 1},
             c.muted);
  }
  int name_left = rect.left + st.s(54), name_right = rect.right - st.s(218);
  ui::text(item.hDC, {name_left, rect.top + st.s(10), name_right, rect.top + st.s(32)}, row.name,
           st.semibold, foreground);
  ui::text(item.hDC, {name_left, rect.top + st.s(32), name_right, rect.bottom - st.s(7)},
           row.detail, st.caption_font,
           selected && st.mode == ui::ThemeMode::HighContrast ? c.selection_text : c.muted);
  RECT behavior{rect.right - st.s(210), rect.top + st.s(10), rect.right - st.s(52),
                rect.top + st.s(32)};
  ui::text(item.hDC, behavior, row.behavior, st.body,
           selected && st.mode == ui::ThemeMode::HighContrast ? c.selection_text
           : row.ignored                                      ? c.accent_label
                                                              : foreground,
           DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
  behavior.top = rect.top + st.s(32);
  behavior.bottom = rect.bottom - st.s(7);
  ui::text(item.hDC, behavior, row.scope, st.caption_font,
           selected && st.mode == ui::ThemeMode::HighContrast ? c.selection_text : c.muted,
           DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
  RECT trigger{rect.right - st.s(42), rect.top + st.s(13), rect.right - st.s(10),
               rect.top + st.s(45)};
  if (selected)
    ui::rounded(item.hDC, trigger, c.surface, c.border, st.s(6));
  for (int line = 0; line < 3; ++line)
    ui::fill(item.hDC,
             {trigger.left + st.s(10), trigger.top + st.s(10 + line * 5), trigger.right - st.s(10),
              trigger.top + st.s(11 + line * 5)},
             c.muted);
  if (item.itemState & ODS_FOCUS) {
    InflateRect(&rect, -st.s(3), -st.s(3));
    DrawFocusRect(item.hDC, &rect);
  }
}

LRESULT CALLBACK list_subclass(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR id,
                               DWORD_PTR data) {
  auto& state = *reinterpret_cast<DialogState*>(data);
  if (message == WM_LBUTTONUP || message == WM_LBUTTONDBLCLK) {
    RECT client{};
    GetClientRect(hwnd, &client);
    LRESULT hit = SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, lparam);
    if (!HIWORD(hit) &&
        (GET_X_LPARAM(lparam) >= client.right - state.style.s(52) || message == WM_LBUTTONDBLCLK)) {
      if (!PostMessageW(state.hwnd, open_row_message, LOWORD(hit), 0))
        spdlog::error("Could not open item actions: {}", GetLastError());
    }
  }
  if (message == WM_CONTEXTMENU) {
    SendMessageW(state.hwnd, message, reinterpret_cast<WPARAM>(hwnd), lparam);
    return 0;
  }
  if (message == WM_KEYDOWN && (wparam == VK_APPS || wparam == VK_RETURN || wparam == VK_SPACE ||
                                (wparam == VK_F10 && (GetKeyState(VK_SHIFT) & 0x8000)))) {
    LRESULT selected = SendMessageW(hwnd, LB_GETCURSEL, 0, 0);
    if (selected != LB_ERR && !PostMessageW(state.hwnd, open_row_message, selected, 0))
      spdlog::error("Could not open keyboard item actions: {}", GetLastError());
    return 0;
  }
  if (message == WM_NCDESTROY && !RemoveWindowSubclass(hwnd, list_subclass, id))
    spdlog::error("Could not release list subclass: {}", GetLastError());
  LRESULT result = DefSubclassProc(hwnd, message, wparam, lparam);
  if (message == WM_PAINT && state.rows.empty()) {
    HDC dc = GetDC(hwnd);
    RECT rect{};
    GetClientRect(hwnd, &rect);
    rect.top += state.style.s(30);
    ui::text(dc, rect,
             state.query.empty() ? L"No windows or rules in this view."
                                 : L"No matches. Try a different search or filter.",
             state.style.body, state.style.colors.muted,
             DT_CENTER | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
    ReleaseDC(hwnd, dc);
  }
  return result;
}

LRESULT CALLBACK dialog_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  auto* state = reinterpret_cast<DialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    state = static_cast<std::unique_ptr<DialogState>*>(
                reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams)
                ->release();
    state->hwnd = hwnd;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
  }
  if (!state)
    return DefWindowProcW(hwnd, message, wparam, lparam);
  switch (message) {
  case WM_CREATE: {
    state->style.update(hwnd);
    state->search = CreateWindowExW(
        0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id_search)), nullptr, nullptr);
    state->list = CreateWindowExW(
        0, L"LISTBOX", L"Windows and ignore rules",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_OWNERDRAWFIXED |
            LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT,
        0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id_list)), nullptr, nullptr);
    if (!state->search || !state->list)
      return -1;
    if (!SetWindowSubclass(state->list, list_subclass, 1, reinterpret_cast<DWORD_PTR>(state)))
      return -1;
    for (auto [id, label] :
         {std::pair{id_windows, L"Open windows"}, std::pair{id_rules, L"Saved rules"},
          std::pair{id_all, L"All"}, std::pair{id_tiled, L"Tiled"},
          std::pair{id_ignored, L"Ignored"}, std::pair{id_refresh, L"Refresh"},
          std::pair{id_config, L"Open config file"}, std::pair{id_undo, L"Undo"},
          std::pair{id_create, L"Create config"}, std::pair{id_retry, L"Retry"},
          std::pair{IDCANCEL, L"Done"}}) {
      HWND button = CreateWindowExW(
          0, L"BUTTON", label, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0, 0, hwnd,
          reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
      if (!button)
        return -1;
      state->buttons[id] = button;
    }
    if (!CreateWindowExW(0, L"STATIC", state->message.c_str(),
                         WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX, 0, 0, 0, 0, hwnd,
                         reinterpret_cast<HMENU>(2112), nullptr, nullptr))
      return -1;
    ShowWindow(state->buttons[id_undo], SW_HIDE);
    ShowWindow(state->buttons[id_retry], SW_HIDE);
    HWND previous = HWND_TOP;
    for (int id : {id_windows, id_rules, id_create, id_search, id_all, id_tiled, id_ignored,
                   id_refresh, id_list, id_config, id_undo, id_retry, IDCANCEL}) {
      HWND control = GetDlgItem(hwnd, id);
      if (!SetWindowPos(control, previous, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE))
        spdlog::debug("Could not set dialog tab order: {}", GetLastError());
      previous = control;
    }
    update_style(*state);
    refresh(*state);
    return 0;
  }
  case WM_SIZE:
    layout(*state);
    return 0;
  case WM_GETMINMAXINFO: {
    auto* limits = reinterpret_cast<MINMAXINFO*>(lparam);
    limits->ptMinTrackSize = {state->style.s(680), state->style.s(530)};
    return 0;
  }
  case WM_DPICHANGED: {
    auto* rect = reinterpret_cast<RECT*>(lparam);
    SetWindowPos(hwnd, nullptr, rect->left, rect->top, rect->right - rect->left,
                 rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
    [[fallthrough]];
  }
  case WM_THEMECHANGED:
  case WM_SETTINGCHANGE:
  case WM_SYSCOLORCHANGE:
    update_style(*state);
    return 0;
  case WM_ERASEBKGND:
    return 1;
  case WM_PAINT: {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(hwnd, &paint);
    RECT rect{};
    GetClientRect(hwnd, &rect);
    const auto& st = state->style;
    const auto& c = st.colors;
    int margin = st.s(24);
    ui::fill(dc, rect, c.background);
    ui::text(dc, {margin, st.s(13), rect.right - margin, st.s(32)}, L"win-tiler", st.caption_font,
             c.muted);
    ui::text(dc, {margin, st.s(35), rect.right - margin, st.s(66)}, L"Ignored windows", st.title,
             c.foreground);
    ui::text(dc, {margin, st.s(70), rect.right - margin, st.s(92)},
             L"Set how each window fits into your layout.", st.body, c.muted);
    ui::fill(dc, {margin, st.s(139), rect.right - margin, st.s(139) + 1}, c.border);
    int offset = state->path && !state->exists ? st.s(56) : 0;
    if (offset) {
      ui::text(dc, {margin, st.s(148), rect.right - margin - st.s(174), st.s(169)},
               L"No config file yet", st.semibold, c.foreground);
      ui::text(dc, {margin, st.s(170), rect.right - margin - st.s(174), st.s(193)},
               L"Your first rule creates it automatically.", st.caption_font, c.muted);
    }
    RECT search{margin, st.s(150) + offset, rect.right - margin - st.s(278), st.s(185) + offset};
    ui::rounded(dc, search, c.surface, c.border, st.s(8));
    ui::text(dc, {margin + st.s(10), st.s(195) + offset, rect.right - margin, st.s(217) + offset},
             state->saved_view ? L"SAVED RULE" : L"WINDOW", st.caption_font, c.muted);
    ui::text(dc,
             {rect.right - margin - st.s(210), st.s(195) + offset, rect.right - margin - st.s(52),
              st.s(217) + offset},
             state->saved_view ? L"MATCH" : L"BEHAVIOR", st.caption_font, c.muted,
             DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    ui::fill(dc,
             {margin, rect.bottom - st.s(100), rect.right - margin, rect.bottom - st.s(100) + 1},
             c.border);
    EndPaint(hwnd, &paint);
    return 0;
  }
  case WM_CTLCOLORSTATIC:
  case WM_CTLCOLOREDIT:
  case WM_CTLCOLORLISTBOX: {
    HDC dc = reinterpret_cast<HDC>(wparam);
    bool edit = message == WM_CTLCOLOREDIT;
    SetBkColor(dc, edit ? state->style.colors.surface : state->style.colors.background);
    SetTextColor(dc, message == WM_CTLCOLORSTATIC
                         ? (state->error ? state->style.colors.error : state->style.colors.muted)
                         : state->style.colors.foreground);
    return reinterpret_cast<LRESULT>(edit ? state->style.surface : state->style.background);
  }
  case WM_MEASUREITEM: {
    auto* item = reinterpret_cast<MEASUREITEMSTRUCT*>(lparam);
    if (item->CtlType == ODT_LISTBOX) {
      item->itemHeight = state->style.s(64);
      return TRUE;
    }
    break;
  }
  case WM_DRAWITEM: {
    const auto& item = *reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
    if (item.CtlType == ODT_LISTBOX) {
      draw_row(*state, item);
      return TRUE;
    }
    if (item.CtlType == ODT_BUTTON) {
      int id = static_cast<int>(item.CtlID);
      bool active = (id == id_windows && !state->saved_view) ||
                    (id == id_rules && state->saved_view) ||
                    (id >= id_all && id <= id_ignored && id - id_all == state->filter);
      draw_button(item, state->style, active, id != IDCANCEL && id != id_create, id == IDCANCEL);
      return TRUE;
    }
    break;
  }
  case open_row_message:
    show_row_menu(*state, static_cast<int>(wparam));
    return 0;
  case WM_CONTEXTMENU: {
    int index = static_cast<int>(SendMessageW(state->list, LB_GETCURSEL, 0, 0));
    if (lparam == -1)
      show_row_menu(*state, index);
    else if (reinterpret_cast<HWND>(wparam) == state->list) {
      POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)}, client = point;
      ScreenToClient(state->list, &client);
      LRESULT hit = SendMessageW(state->list, LB_ITEMFROMPOINT, 0, MAKELPARAM(client.x, client.y));
      if (!HIWORD(hit))
        show_row_menu(*state, LOWORD(hit), point);
    }
    return 0;
  }
  case WM_COMMAND: {
    int id = LOWORD(wparam);
    if (id == IDCANCEL) {
      DestroyWindow(hwnd);
      return 0;
    }
    if (id == id_search && HIWORD(wparam) == EN_CHANGE && !state->loading) {
      state->query = window_text(state->search);
      rebuild_rows(*state);
      return 0;
    }
    if (id == id_windows || id == id_rules) {
      state->saved_view = id == id_rules;
      state->filter = 0;
      state->query.clear();
      SetWindowTextW(state->search, L"");
      refresh(*state);
      return 0;
    }
    if (id >= id_all && id <= id_ignored) {
      state->filter = id - id_all;
      rebuild_rows(*state);
      return 0;
    }
    if (id == id_refresh) {
      state->pending.reset();
      feedback(*state, L"Window list and saved rules refreshed.");
      refresh(*state);
      return 0;
    }
    if (id == id_config) {
      if (!open_config(*state))
        spdlog::debug("No config editor was opened");
      return 0;
    }
    if (id == id_undo && state->undo) {
      apply_edit(*state, {state->undo->added, state->undo->removed, {}, true});
      return 0;
    }
    if (id == id_retry && state->pending) {
      apply_edit(*state, *state->pending);
      return 0;
    }
    if (id == id_create && state->path) {
      auto created = create_ignore_config(*state->path);
      if (!created) {
        feedback(*state, ui::wide(created.error()), true);
        return 0;
      }
      refresh(*state);
      state->changed(state->options);
      feedback(*state, L"Config created. You can now save ignore rules.");
      return 0;
    }
    return 0;
  }
  case WM_CLOSE:
    DestroyWindow(hwnd);
    return 0;
  case WM_NCDESTROY:
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
    if (dialog_window == hwnd)
      dialog_window = nullptr;
    delete state;
    return DefWindowProcW(hwnd, message, wparam, lparam);
  default:
    break;
  }
  return DefWindowProcW(hwnd, message, wparam, lparam);
}
#ifndef DOCTEST_CONFIG_DISABLE
TEST_CASE("Item menus do not expose the global config action") {
  HWND owner = CreateWindowExW(0, L"STATIC", L"Item menu scope regression", 0, 0, 0, 0, 0, nullptr,
                               nullptr, GetModuleHandleW(nullptr), nullptr);
  REQUIRE(owner != nullptr);
  {
    DialogState state;
    state.hwnd = owner;
    state.exists = state.readable = true;
    Row row;
    winapi::WindowManagementSnapshot window;
    window.process_name = "fastpdf.exe";
    window.title = "FastPDF";
    row.window = window;
    SUBCASE("open window") {
    }
    SUBCASE("saved rule") {
      state.saved_view = true;
      row.window.reset();
      row.entry = IgnoreRuleEntry{{IgnoreRuleKind::Process, "fastpdf.exe", {}}, false};
    }
    SUBCASE("overlapping rules and submenus") {
      state.rules = {{{IgnoreRuleKind::Process, "fastpdf.exe", {}}, false},
                     {{IgnoreRuleKind::Title, {}, "FastPDF"}, false}};
    }
    ui::PopupMenu menu(owner);
    std::vector<MenuAction> actions;
    populate_row_menu(state, row, menu, actions);
    REQUIRE(menu.ok);
    CHECK(std::none_of(menu.items.begin(), menu.items.end(),
                       [](const auto& item) { return item->label == L"Open config file"; }));
    REQUIRE_FALSE(actions.empty());
    CHECK(actions.back().kind == MenuActionKind::Copy);
  }
  CHECK(DestroyWindow(owner) != FALSE);
}

TEST_CASE("Rule menus allow the first assignment without a config file") {
  HWND owner = CreateWindowExW(0, L"STATIC", L"Rule menu regression", 0, 0, 0, 0, 0, nullptr,
                               nullptr, GetModuleHandleW(nullptr), nullptr);
  REQUIRE(owner != nullptr);
  {
    DialogState state;
    state.hwnd = owner;
    state.path = std::filesystem::temp_directory_path() / "win-tiler-menu-regression.toml";
    Row row;
    winapi::WindowManagementSnapshot window;
    window.process_name = "fastpdf.exe";
    window.title = "FastPDF";
    row.window = window;
    bool expected_enabled = true;
    SUBCASE("missing config enables assignments") {
    }
    SUBCASE("valid config enables assignments") {
      state.exists = state.readable = true;
    }
    SUBCASE("invalid existing config stays protected") {
      state.exists = true;
      expected_enabled = false;
    }
    SUBCASE("missing config path explains unavailable editing") {
      state.path.reset();
      expected_enabled = false;
    }
    SUBCASE("built-in rules stay protected") {
      state.exists = state.readable = true;
      row.entry = IgnoreRuleEntry{{IgnoreRuleKind::Process, "fastpdf.exe", {}}, true};
      expected_enabled = false;
    }
    ui::PopupMenu menu(owner);
    std::vector<MenuAction> actions;
    populate_row_menu(state, row, menu, actions);
    REQUIRE(menu.ok);
    int assignments = 0;
    for (size_t i = 0; i < actions.size(); ++i) {
      if (actions[i].kind != MenuActionKind::Change || !actions[i].after)
        continue;
      const UINT flags = GetMenuState(menu.handle, 4000 + static_cast<UINT>(i), MF_BYCOMMAND);
      REQUIRE(flags != static_cast<UINT>(-1));
      CHECK(((flags & (MF_GRAYED | MF_DISABLED)) == 0) == expected_enabled);
      ++assignments;
    }
    CHECK(assignments == 4);
  }
  CHECK(DestroyWindow(owner) != FALSE);
}
#endif
} // namespace

void show_ignore_dialog(HWND owner, std::optional<std::filesystem::path> path,
                        IgnoreOptions options, std::function<void(const IgnoreOptions&)> changed) {
  if (dialog_window && IsWindow(dialog_window)) {
    ShowWindow(dialog_window, SW_RESTORE);
    if (!SetForegroundWindow(dialog_window))
      spdlog::debug("Could not foreground ignore dialog");
    return;
  }
  INITCOMMONCONTROLSEX init{sizeof(INITCOMMONCONTROLSEX), ICC_STANDARD_CLASSES};
  if (!InitCommonControlsEx(&init)) {
    spdlog::error("Could not initialize dialog controls: {}", GetLastError());
    return;
  }
  WNDCLASSEXW klass{sizeof(WNDCLASSEXW)};
  klass.lpfnWndProc = dialog_proc;
  klass.hInstance = GetModuleHandleW(nullptr);
  klass.lpszClassName = dialog_class;
  klass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  klass.hIcon = LoadIconW(klass.hInstance, MAKEINTRESOURCEW(IDI_APP_ICON));
  klass.hIconSm = klass.hIcon;
  if (!RegisterClassExW(&klass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    spdlog::error("Could not register ignore dialog: {}", GetLastError());
    return;
  }
  auto pending_state = std::make_unique<DialogState>();
  auto* state = pending_state.get();
  state->path = std::move(path);
  state->options = std::move(options);
  state->changed = std::move(changed);
  POINT cursor{};
  GetCursorPos(&cursor);
  MONITORINFO monitor{sizeof(MONITORINFO)};
  GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), &monitor);
  UINT dpi = GetDpiForWindow(owner);
  if (!dpi)
    dpi = 96;
  int width =
      (std::min)(ui::scale(960, dpi), static_cast<int>(monitor.rcWork.right - monitor.rcWork.left));
  int height =
      (std::min)(ui::scale(780, dpi), static_cast<int>(monitor.rcWork.bottom - monitor.rcWork.top));
  HWND window = CreateWindowExW(
      WS_EX_CONTROLPARENT, dialog_class, L"Ignored windows - win-tiler",
      WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
      monitor.rcWork.left + (monitor.rcWork.right - monitor.rcWork.left - width) / 2,
      monitor.rcWork.top + (monitor.rcWork.bottom - monitor.rcWork.top - height) / 2, width, height,
      owner, nullptr, klass.hInstance, &pending_state);
  // WM_NCDESTROY releases transferred state; earlier failures retain the unique_ptr.
  if (!window) {
    spdlog::error("Could not create ignore dialog: {}", GetLastError());
    return;
  }
  dialog_window = window;
  ShowWindow(window, SW_SHOW);
  UpdateWindow(window);
  SetFocus(state->list);
}

bool handle_ignore_dialog_message(MSG& message) {
  if (!dialog_window || !IsWindow(dialog_window))
    return false;
  if (message.hwnd != dialog_window && !IsChild(dialog_window, message.hwnd))
    return false;
  auto* state = reinterpret_cast<DialogState*>(GetWindowLongPtrW(dialog_window, GWLP_USERDATA));
  if (!state)
    return false;
  if (message.message == WM_KEYDOWN) {
    if (message.wParam == VK_F5) {
      SendMessageW(dialog_window, WM_COMMAND, id_refresh, 0);
      return true;
    }
    if ((GetKeyState(VK_CONTROL) & 0x8000) && message.wParam == 'F') {
      SetFocus(state->search);
      return true;
    }
    if (message.hwnd == state->list &&
        (message.wParam == VK_RETURN || message.wParam == VK_SPACE || message.wParam == VK_APPS ||
         (message.wParam == VK_F10 && (GetKeyState(VK_SHIFT) & 0x8000)))) {
      SendMessageW(state->list, message.message, message.wParam, message.lParam);
      return true;
    }
  }
  return IsDialogMessageW(dialog_window, &message) != FALSE;
}

void close_ignore_dialog() {
  if (dialog_window && IsWindow(dialog_window))
    DestroyWindow(dialog_window);
}
} // namespace wintiler
