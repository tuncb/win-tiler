#include "ui_theme.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <shellscalingapi.h>
#include <spdlog/spdlog.h>

#include <cwctype>

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Dwmapi.lib")
#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "Shcore.lib")

namespace wintiler::ui {

ThemeMode choose_theme(std::optional<bool> apps_light, std::optional<bool> system_light,
                       bool high_contrast, ThemeSurface surface) {
  if (high_contrast)
    return ThemeMode::HighContrast;
  bool light = surface == ThemeSurface::System ? system_light.value_or(apps_light.value_or(true))
                                               : apps_light.value_or(true);
  return light ? ThemeMode::Light : ThemeMode::Dark;
}

ThemeMode system_theme(ThemeSurface surface) {
  HIGHCONTRASTW contrast{sizeof(HIGHCONTRASTW), 0, nullptr};
  bool high_contrast = SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) &&
                       (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
  auto read_light = [](const wchar_t* name) -> std::optional<bool> {
    DWORD value = 1, size = sizeof(value);
    LSTATUS status = RegGetValueW(
        HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        name, RRF_RT_REG_DWORD, nullptr, &value, &size);
    if (status != ERROR_SUCCESS)
      return std::nullopt;
    return value != 0;
  };
  return choose_theme(read_light(L"AppsUseLightTheme"), read_light(L"SystemUsesLightTheme"),
                      high_contrast, surface);
}

Palette palette_for(ThemeMode mode, ThemeSurface surface) {
  if (mode == ThemeMode::HighContrast) {
    return {GetSysColor(COLOR_WINDOW),     GetSysColor(COLOR_WINDOW),
            GetSysColor(COLOR_MENU),       GetSysColor(COLOR_WINDOWTEXT),
            GetSysColor(COLOR_WINDOWTEXT), GetSysColor(COLOR_WINDOWTEXT),
            GetSysColor(COLOR_HIGHLIGHT),  GetSysColor(COLOR_HIGHLIGHTTEXT),
            GetSysColor(COLOR_HIGHLIGHT),  GetSysColor(COLOR_HIGHLIGHTTEXT),
            GetSysColor(COLOR_WINDOWTEXT), GetSysColor(COLOR_WINDOWTEXT)};
  }
  // The icon supplies the navy foundation, cyan dark accent, and amber light accent.
  // Text accents use a darker amber in light mode to stay readable on pale surfaces.
  Palette colors =
      mode == ThemeMode::Dark
          ? Palette{RGB(11, 23, 38),    RGB(17, 34, 53), RGB(22, 43, 64),   RGB(232, 240, 250),
                    RGB(167, 186, 205), RGB(44, 67, 88), RGB(23, 61, 88),   RGB(232, 240, 250),
                    RGB(54, 197, 240),  RGB(6, 33, 58),  RGB(54, 197, 240), RGB(255, 156, 170)}
          : Palette{RGB(251, 250, 246), RGB(244, 241, 232), RGB(255, 255, 255), RGB(43, 37, 26),
                    RGB(109, 98, 83),   RGB(221, 212, 197), RGB(255, 240, 205), RGB(43, 37, 26),
                    RGB(255, 176, 32),  RGB(38, 25, 0),     RGB(138, 85, 0),    RGB(181, 45, 67)};
  if (surface == ThemeSurface::System) {
    // The tray uses quieter surfaces while keeping the same selection and accent colors.
    colors.background = colors.surface = colors.raised =
        mode == ThemeMode::Dark ? RGB(21, 34, 50) : RGB(255, 255, 255);
  }
  return colors;
}

int scale(int value, UINT dpi) {
  return MulDiv(value, static_cast<int>(dpi), 96);
}

HFONT make_font(int size, UINT dpi, int weight) {
  HFONT font = CreateFontW(-scale(size, dpi), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
  if (!font)
    spdlog::error("Could not create UI font: {}", GetLastError());
  return font;
}

std::wstring wide(std::string_view value) {
  if (value.empty())
    return {};
  int length =
      MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
  std::wstring result(static_cast<size_t>(length), L'\0');
  if (length && !MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                                     result.data(), length))
    spdlog::error("UI UTF-8 conversion failed");
  return result;
}

std::string utf8(std::wstring_view value) {
  if (value.empty())
    return {};
  int length = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                                   nullptr, 0, nullptr, nullptr);
  std::string result(static_cast<size_t>(length), '\0');
  if (length && !WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                                     result.data(), length, nullptr, nullptr))
    spdlog::error("UI UTF-16 conversion failed");
  return result;
}

void fill(HDC dc, RECT rect, COLORREF color) {
  SetDCBrushColor(dc, color);
  FillRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
}

void text(HDC dc, RECT rect, std::wstring_view value, HFONT font, COLORREF color, UINT flags) {
  HGDIOBJ previous = SelectObject(dc, font ? font : GetStockObject(DEFAULT_GUI_FONT));
  int background = SetBkMode(dc, TRANSPARENT);
  COLORREF old_color = SetTextColor(dc, color);
  DrawTextW(dc, value.data(), static_cast<int>(value.size()), &rect, flags);
  SetTextColor(dc, old_color);
  SetBkMode(dc, background);
  SelectObject(dc, previous);
}

void rounded(HDC dc, RECT rect, COLORREF background, COLORREF border, int radius) {
  HGDIOBJ old_brush = SelectObject(dc, GetStockObject(DC_BRUSH));
  HGDIOBJ old_pen = SelectObject(dc, GetStockObject(DC_PEN));
  SetDCBrushColor(dc, background);
  SetDCPenColor(dc, border);
  RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
  SelectObject(dc, old_pen);
  SelectObject(dc, old_brush);
}

void apply_titlebar(HWND hwnd, ThemeMode mode) {
  BOOL dark = mode == ThemeMode::Dark;
  HRESULT result = DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
  if (FAILED(result))
    spdlog::debug("Dark title bar unavailable: {}", static_cast<long>(result));
}

PopupMenu::PopupMenu(HWND window, ThemeSurface surface)
    : owner(window), mode(system_theme(surface)), colors(palette_for(mode, surface)) {
  dpi = GetDpiForWindow(owner);
  if (!dpi)
    dpi = 96;
  if (surface == ThemeSurface::System) {
    POINT point{};
    if (GetCursorPos(&point)) {
      UINT x = dpi, y = dpi;
      if (SUCCEEDED(GetDpiForMonitor(MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST),
                                     MDT_EFFECTIVE_DPI, &x, &y)))
        dpi = x;
    }
  }
  handle = CreatePopupMenu();
  font = make_font(13, dpi);
  small_font = make_font(11, dpi);
  background = CreateSolidBrush(colors.raised);
  ok = handle && font && small_font && background;
  if (!ok)
    spdlog::error("Could not create popup menu resources: {}", GetLastError());
  if (handle)
    style_menu(handle);
}

PopupMenu::~PopupMenu() {
  if (handle && !DestroyMenu(handle))
    spdlog::error("Failed to destroy popup menu: {}", GetLastError());
  for (HGDIOBJ object : {static_cast<HGDIOBJ>(font), static_cast<HGDIOBJ>(small_font),
                         static_cast<HGDIOBJ>(background)}) {
    if (object && !DeleteObject(object))
      spdlog::error("Failed to release menu resource: {}", GetLastError());
  }
}

void PopupMenu::style_menu(HMENU menu) {
  MENUINFO info{};
  info.cbSize = sizeof(info);
  info.fMask = MIM_BACKGROUND | MIM_STYLE;
  info.hbrBack = background;
  info.dwStyle = MNS_CHECKORBMP;
  if (!SetMenuInfo(menu, &info)) {
    ok = false;
    spdlog::error("Failed to style menu: {}", GetLastError());
  }
}

void PopupMenu::append(UINT id, std::wstring label, std::wstring description, bool enabled,
                       bool checked, HMENU parent) {
  if (!parent)
    parent = handle;
  auto item = std::make_unique<MenuItem>();
  item->label = std::move(label);
  item->description = std::move(description);
  item->accessible.dwMSAASignature = MSAA_MENU_SIG;
  item->accessible.cchWText = static_cast<DWORD>(item->label.size());
  item->accessible.pszWText = item->label.data();
  MENUITEMINFOW info{};
  info.cbSize = sizeof(info);
  info.fMask = MIIM_FTYPE | MIIM_ID | MIIM_DATA | MIIM_STRING | MIIM_STATE;
  info.fType = MFT_OWNERDRAW;
  info.wID = id;
  info.dwItemData = reinterpret_cast<ULONG_PTR>(item.get());
  info.dwTypeData = item->label.data();
  info.fState = (enabled ? MFS_ENABLED : MFS_DISABLED) | (checked ? MFS_CHECKED : MFS_UNCHECKED);
  if (!InsertMenuItemW(parent, static_cast<UINT>(-1), TRUE, &info)) {
    ok = false;
    spdlog::error("Failed to append themed menu item: {}", GetLastError());
  }
  items.push_back(std::move(item));
}

void PopupMenu::separator(HMENU parent) {
  if (!parent)
    parent = handle;
  auto item = std::make_unique<MenuItem>();
  item->separator = true;
  MENUITEMINFOW info{};
  info.cbSize = sizeof(info);
  info.fMask = MIIM_FTYPE | MIIM_DATA;
  info.fType = MFT_OWNERDRAW | MFT_SEPARATOR;
  info.dwItemData = reinterpret_cast<ULONG_PTR>(item.get());
  if (!InsertMenuItemW(parent, static_cast<UINT>(-1), TRUE, &info)) {
    ok = false;
    spdlog::error("Failed to append menu separator: {}", GetLastError());
  }
  items.push_back(std::move(item));
}

HMENU PopupMenu::submenu(std::wstring label, HMENU parent) {
  if (!parent)
    parent = handle;
  HMENU child = CreatePopupMenu();
  if (!child) {
    ok = false;
    spdlog::error("Failed to create submenu: {}", GetLastError());
    return nullptr;
  }
  style_menu(child);
  append(0, std::move(label), {}, true, false, parent);
  items.back()->submenu = true;
  MENUITEMINFOW info{};
  info.cbSize = sizeof(info);
  info.fMask = MIIM_SUBMENU;
  info.hSubMenu = child;
  if (!SetMenuItemInfoW(parent, static_cast<UINT>(GetMenuItemCount(parent) - 1), TRUE, &info)) {
    ok = false;
    spdlog::error("Failed to attach submenu: {}", GetLastError());
    if (!DestroyMenu(child))
      spdlog::error("Failed to release unattached submenu: {}", GetLastError());
    return nullptr;
  }
  return child;
}

void PopupMenu::measure(MEASUREITEMSTRUCT& measured) {
  auto* item = reinterpret_cast<MenuItem*>(measured.itemData);
  if (!item)
    return;
  if (item->separator) {
    measured.itemWidth = 1;
    measured.itemHeight = scale(9, dpi);
    return;
  }
  HDC dc = GetDC(owner);
  if (!dc) {
    measured.itemWidth = scale(300, dpi);
    measured.itemHeight = scale(36, dpi);
    return;
  }
  auto measure_text = [&](const std::wstring& value, HFONT selected) {
    HGDIOBJ previous = SelectObject(dc, selected);
    SIZE size{};
    GetTextExtentPoint32W(dc, value.data(), static_cast<int>(value.size()), &size);
    SelectObject(dc, previous);
    return size.cx;
  };
  int width =
      (std::max)(measure_text(item->label, font), measure_text(item->description, small_font));
  measured.itemWidth =
      static_cast<UINT>((std::clamp)(width + scale(66, dpi), scale(220, dpi), scale(430, dpi)));
  measured.itemHeight = scale(item->description.empty() ? 34 : 52, dpi);
  ReleaseDC(owner, dc);
}

void PopupMenu::draw(const DRAWITEMSTRUCT& drawn) {
  auto* item = reinterpret_cast<MenuItem*>(drawn.itemData);
  if (!item)
    return;
  RECT rect = drawn.rcItem;
  fill(drawn.hDC, rect, colors.raised);
  if (item->separator) {
    rect.left += scale(12, dpi);
    rect.right -= scale(12, dpi);
    rect.top = (rect.top + rect.bottom) / 2;
    rect.bottom = rect.top + 1;
    fill(drawn.hDC, rect, colors.border);
    return;
  }
  const bool selected = (drawn.itemState & ODS_SELECTED) != 0;
  const bool disabled = (drawn.itemState & (ODS_DISABLED | ODS_GRAYED)) != 0;
  RECT inset = rect;
  InflateRect(&inset, -scale(4, dpi), -scale(2, dpi));
  if (selected && !disabled)
    rounded(drawn.hDC, inset, colors.selection, colors.selection, scale(8, dpi));
  COLORREF foreground = disabled   ? colors.muted
                        : selected ? colors.selection_text
                                   : colors.foreground;
  RECT label = rect;
  label.left += scale(34, dpi);
  label.right -= scale(28, dpi);
  if (!item->description.empty()) {
    label.top += scale(5, dpi);
    label.bottom = label.top + scale(21, dpi);
  }
  text(drawn.hDC, label, item->label, font, foreground,
       DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
  if (!item->description.empty()) {
    RECT description = label;
    description.top = label.bottom;
    description.bottom = rect.bottom - scale(4, dpi);
    text(drawn.hDC, description, item->description, small_font,
         disabled                                      ? colors.muted
         : selected && mode == ThemeMode::HighContrast ? colors.selection_text
                                                       : colors.muted);
  }
  if (drawn.itemState & ODS_CHECKED) {
    RECT check = rect;
    check.right = check.left + scale(30, dpi);
    text(drawn.hDC, check, L"\x2713", font, foreground, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
  }
  if (item->submenu) {
    RECT arrow = rect;
    arrow.left = arrow.right - scale(24, dpi);
    text(drawn.hDC, arrow, L"\x203a", font, foreground, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
  }
}

LRESULT CALLBACK PopupMenu::subclass(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam,
                                     UINT_PTR id, DWORD_PTR data) {
  auto& menu = *reinterpret_cast<PopupMenu*>(data);
  if (message == WM_MEASUREITEM &&
      reinterpret_cast<MEASUREITEMSTRUCT*>(lparam)->CtlType == ODT_MENU) {
    menu.measure(*reinterpret_cast<MEASUREITEMSTRUCT*>(lparam));
    return TRUE;
  }
  if (message == WM_DRAWITEM && reinterpret_cast<DRAWITEMSTRUCT*>(lparam)->CtlType == ODT_MENU) {
    menu.draw(*reinterpret_cast<DRAWITEMSTRUCT*>(lparam));
    return TRUE;
  }
  if (message == WM_SETTINGCHANGE || message == WM_THEMECHANGED || message == WM_SYSCOLORCHANGE) {
    if (!EndMenu())
      spdlog::debug("No active menu to dismiss for theme change");
  }
  if (message == WM_MENUCHAR) {
    HMENU active = reinterpret_cast<HMENU>(lparam);
    std::vector<std::pair<int, bool>> matches;
    int highlighted = -1;
    for (int index = 0; index < GetMenuItemCount(active); ++index) {
      MENUITEMINFOW info{};
      info.cbSize = sizeof(info);
      info.fMask = MIIM_DATA | MIIM_STATE | MIIM_SUBMENU;
      if (GetMenuItemInfoW(active, index, TRUE, &info) && !(info.fState & MFS_DISABLED)) {
        auto* item = reinterpret_cast<MenuItem*>(info.dwItemData);
        if (info.fState & MFS_HILITE)
          highlighted = index;
        if (item && !item->label.empty() &&
            std::towlower(item->label.front()) == std::towlower(LOWORD(wparam)))
          matches.emplace_back(index, info.hSubMenu != nullptr);
      }
    }
    if (matches.size() == 1)
      return MAKELRESULT(matches.front().first, matches.front().second ? MNC_SELECT : MNC_EXECUTE);
    if (!matches.empty()) {
      auto next = std::find_if(matches.begin(), matches.end(),
                               [&](const auto& match) { return match.first > highlighted; });
      return MAKELRESULT((next == matches.end() ? matches.front() : *next).first, MNC_SELECT);
    }
    return MAKELRESULT(0, MNC_IGNORE);
  }
  if (message == WM_NCDESTROY && !RemoveWindowSubclass(hwnd, subclass, id))
    spdlog::error("Failed to remove menu subclass: {}", GetLastError());
  return DefSubclassProc(hwnd, message, wparam, lparam);
}

UINT PopupMenu::track(POINT point) {
  if (!ok)
    return 0;
  UINT_PTR id = reinterpret_cast<UINT_PTR>(this);
  if (!SetWindowSubclass(owner, subclass, id, reinterpret_cast<DWORD_PTR>(this))) {
    spdlog::error("Failed to subclass popup owner: {}", GetLastError());
    return 0;
  }
  UINT command = TrackPopupMenuEx(handle, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, point.x,
                                  point.y, owner, nullptr);
  if (IsWindow(owner) && !RemoveWindowSubclass(owner, subclass, id))
    spdlog::error("Failed to remove popup subclass: {}", GetLastError());
  return command;
}
} // namespace wintiler::ui
