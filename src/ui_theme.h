#pragma once

#include <oleacc.h>
#include <windows.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace wintiler::ui {

enum class ThemeMode { Light, Dark, HighContrast };
enum class ThemeSurface { Application, System };

[[nodiscard]] ThemeMode choose_theme(std::optional<bool> apps_light,
                                     std::optional<bool> system_light, bool high_contrast,
                                     ThemeSurface surface);
[[nodiscard]] ThemeMode system_theme(ThemeSurface surface = ThemeSurface::Application);

struct Palette {
  COLORREF background, surface, raised, foreground, muted, border, selection;
  COLORREF selection_text, accent, accent_text, accent_label, error;
};
[[nodiscard]] Palette palette_for(ThemeMode mode, ThemeSurface surface = ThemeSurface::Application);
[[nodiscard]] int scale(int value, UINT dpi);
[[nodiscard]] HFONT make_font(int size, UINT dpi, int weight = FW_NORMAL);
[[nodiscard]] std::wstring wide(std::string_view text);
[[nodiscard]] std::string utf8(std::wstring_view text);
void fill(HDC dc, RECT rect, COLORREF color);
void text(HDC dc, RECT rect, std::wstring_view value, HFONT font, COLORREF color,
          UINT flags = DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
void rounded(HDC dc, RECT rect, COLORREF background, COLORREF border, int radius);
void apply_titlebar(HWND hwnd, ThemeMode mode);

struct MenuItem {
  // Required first field: lets MSAA expose owner-drawn menu names.
  MSAAMENUINFO accessible{};
  std::wstring label;
  std::wstring description;
  bool separator = false;
  bool submenu = false;
};

// A native HMENU retains Windows keyboard/navigation/accessibility behavior.
// Only its painting is customized; no undocumented uxtheme ordinals are used.
struct PopupMenu {
  HWND owner;
  HMENU handle = nullptr;
  ThemeMode mode;
  Palette colors;
  UINT dpi = 96;
  HFONT font = nullptr;
  HFONT small_font = nullptr;
  HBRUSH background = nullptr;
  bool ok = true;
  std::vector<std::unique_ptr<MenuItem>> items;

  explicit PopupMenu(HWND owner, ThemeSurface surface = ThemeSurface::Application);
  ~PopupMenu();
  PopupMenu(const PopupMenu&) = delete;
  PopupMenu& operator=(const PopupMenu&) = delete;
  void append(UINT id, std::wstring label, std::wstring description = {}, bool enabled = true,
              bool checked = false, HMENU parent = nullptr);
  void separator(HMENU parent = nullptr);
  [[nodiscard]] HMENU submenu(std::wstring label, HMENU parent = nullptr);
  [[nodiscard]] UINT track(POINT point);
  void style_menu(HMENU menu);
  void measure(MEASUREITEMSTRUCT& item);
  void draw(const DRAWITEMSTRUCT& item);
  static LRESULT CALLBACK subclass(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam,
                                   UINT_PTR id, DWORD_PTR data);
};
} // namespace wintiler::ui
