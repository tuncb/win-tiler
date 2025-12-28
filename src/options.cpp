#include "options.h"

#include <spdlog/spdlog.h>

#include <fstream>
#include <toml++/toml.hpp>

namespace wintiler {

namespace {

std::string hotkey_action_to_string(HotkeyAction action) {
  switch (action) {
  case HotkeyAction::NavigateLeft:
    return "NavigateLeft";
  case HotkeyAction::NavigateDown:
    return "NavigateDown";
  case HotkeyAction::NavigateUp:
    return "NavigateUp";
  case HotkeyAction::NavigateRight:
    return "NavigateRight";
  case HotkeyAction::ToggleSplit:
    return "ToggleSplit";
  case HotkeyAction::Exit:
    return "Exit";
  case HotkeyAction::ToggleGlobal:
    return "ToggleGlobal";
  case HotkeyAction::StoreCell:
    return "StoreCell";
  case HotkeyAction::ClearStored:
    return "ClearStored";
  case HotkeyAction::Exchange:
    return "Exchange";
  case HotkeyAction::Move:
    return "Move";
  }
  return "Unknown";
}

std::optional<HotkeyAction> string_to_hotkey_action(const std::string& str) {
  if (str == "NavigateLeft")
    return HotkeyAction::NavigateLeft;
  if (str == "NavigateDown")
    return HotkeyAction::NavigateDown;
  if (str == "NavigateUp")
    return HotkeyAction::NavigateUp;
  if (str == "NavigateRight")
    return HotkeyAction::NavigateRight;
  if (str == "ToggleSplit")
    return HotkeyAction::ToggleSplit;
  if (str == "Exit")
    return HotkeyAction::Exit;
  if (str == "ToggleGlobal")
    return HotkeyAction::ToggleGlobal;
  if (str == "StoreCell")
    return HotkeyAction::StoreCell;
  if (str == "ClearStored")
    return HotkeyAction::ClearStored;
  if (str == "Exchange")
    return HotkeyAction::Exchange;
  if (str == "Move")
    return HotkeyAction::Move;
  return std::nullopt;
}

} // anonymous namespace

IgnoreOptions get_default_ignore_options() {
  IgnoreOptions options;
  options.ignored_processes = {
      "TextInputHost.exe",       "ApplicationFrameHost.exe",
      "Microsoft.CmdPal.UI.exe", "PowerToys.PowerLauncher.exe",
      "win-tiler.exe",
  };
  // options.ignored_window_titles = {"Windows Widgets", "MSN"};
  options.ignored_window_titles = {};
  options.ignored_process_title_pairs = {
      {"SystemSettings.exe", "Settings"},
      {"explorer.exe", "Program Manager"},
      {"explorer.exe", "System tray overflow window."},
      {"explorer.exe", "PopupHost"},
      {"claude.exe", "Title: Claude"},
      {"WidgetBoard.exe", "Windows Widgets"},
      {"msedgewebview2.exe", "MSN"},
  };
  options.small_window_barrier = SmallWindowBarrier{50, 50};
  return options;
}

GlobalOptions get_default_global_options() {
  GlobalOptions options;
  options.ignoreOptions = get_default_ignore_options();
  options.keyboardOptions.bindings = {
      {HotkeyAction::NavigateLeft, "super+shift+h"}, {HotkeyAction::NavigateDown, "super+shift+j"},
      {HotkeyAction::NavigateUp, "super+shift+k"},   {HotkeyAction::NavigateRight, "super+shift+l"},
      {HotkeyAction::ToggleSplit, "super+shift+y"},  {HotkeyAction::Exit, "super+shift+escape"},
      {HotkeyAction::ToggleGlobal, "super+shift+;"}, {HotkeyAction::StoreCell, "super+shift+["},
      {HotkeyAction::ClearStored, "super+shift+]"},  {HotkeyAction::Exchange, "super+shift+,"},
      {HotkeyAction::Move, "super+shift+."},
  };
  return options;
}

WriteResult write_options_toml(const GlobalOptions& options,
                               const std::filesystem::path& filepath) {
  try {
    toml::table root;

    // Build ignore section
    toml::table ignore;

    toml::array processes;
    for (const auto& p : options.ignoreOptions.ignored_processes) {
      processes.push_back(p);
    }
    ignore.insert("processes", processes);

    toml::array window_titles;
    for (const auto& t : options.ignoreOptions.ignored_window_titles) {
      window_titles.push_back(t);
    }
    ignore.insert("window_titles", window_titles);

    toml::array process_title_pairs;
    for (const auto& [process, title] : options.ignoreOptions.ignored_process_title_pairs) {
      toml::table pair;
      pair.insert("process", process);
      pair.insert("title", title);
      process_title_pairs.push_back(pair);
    }
    ignore.insert("process_title_pairs", process_title_pairs);

    if (options.ignoreOptions.small_window_barrier) {
      toml::table barrier;
      barrier.insert("width", options.ignoreOptions.small_window_barrier->width);
      barrier.insert("height", options.ignoreOptions.small_window_barrier->height);
      ignore.insert("small_window_barrier", barrier);
    }

    root.insert("ignore", ignore);

    // Build keyboard section
    toml::table keyboard;
    toml::array bindings;
    for (const auto& binding : options.keyboardOptions.bindings) {
      toml::table b;
      b.insert("action", hotkey_action_to_string(binding.action));
      b.insert("hotkey", binding.hotkey);
      bindings.push_back(b);
    }
    keyboard.insert("bindings", bindings);
    root.insert("keyboard", keyboard);

    // Build gap section
    toml::table gap;
    gap.insert("horizontal", options.gapOptions.horizontal);
    gap.insert("vertical", options.gapOptions.vertical);
    root.insert("gap", gap);

    // Build render section
    toml::table render;
    auto colorToArray = [](const overlay::Color& c) {
      toml::array arr;
      arr.push_back(static_cast<int64_t>(c.r));
      arr.push_back(static_cast<int64_t>(c.g));
      arr.push_back(static_cast<int64_t>(c.b));
      arr.push_back(static_cast<int64_t>(c.a));
      return arr;
    };
    render.insert("normal_color", colorToArray(options.visualizationOptions.normalColor));
    render.insert("selected_color", colorToArray(options.visualizationOptions.selectedColor));
    render.insert("stored_color", colorToArray(options.visualizationOptions.storedColor));
    render.insert("border_width", options.visualizationOptions.borderWidth);
    render.insert("toast_font_size", options.visualizationOptions.toastFontSize);
    render.insert("toast_duration_ms", options.visualizationOptions.toastDurationMs);
    root.insert("render", render);

    // Write to file
    std::ofstream file(filepath);
    if (!file) {
      return WriteResult{false, "Failed to open file for writing: " + filepath.string()};
    }
    file << root;
    return WriteResult{true, ""};
  } catch (const std::exception& e) {
    return WriteResult{false, std::string("Error writing TOML: ") + e.what()};
  }
}

ReadResult read_options_toml(const std::filesystem::path& filepath) {
  try {
    auto tbl = toml::parse_file(filepath.string());
    GlobalOptions options;

    // Parse ignore section
    if (auto ignore = tbl["ignore"].as_table()) {
      if (auto processes = (*ignore)["processes"].as_array()) {
        for (const auto& p : *processes) {
          if (auto str = p.as_string()) {
            options.ignoreOptions.ignored_processes.push_back(str->get());
          }
        }
      }

      if (auto titles = (*ignore)["window_titles"].as_array()) {
        for (const auto& t : *titles) {
          if (auto str = t.as_string()) {
            options.ignoreOptions.ignored_window_titles.push_back(str->get());
          }
        }
      }

      if (auto pairs = (*ignore)["process_title_pairs"].as_array()) {
        for (const auto& pair : *pairs) {
          if (auto tbl = pair.as_table()) {
            auto process = (*tbl)["process"].as_string();
            auto title = (*tbl)["title"].as_string();
            if (process && title) {
              options.ignoreOptions.ignored_process_title_pairs.emplace_back(process->get(),
                                                                             title->get());
            }
          }
        }
      }

      if (auto barrier = (*ignore)["small_window_barrier"].as_table()) {
        auto width = (*barrier)["width"].as_integer();
        auto height = (*barrier)["height"].as_integer();
        if (width && height) {
          options.ignoreOptions.small_window_barrier =
              SmallWindowBarrier{static_cast<int>(width->get()), static_cast<int>(height->get())};
        }
      }
    }

    // Parse keyboard section
    if (auto keyboard = tbl["keyboard"].as_table()) {
      if (auto bindings = (*keyboard)["bindings"].as_array()) {
        for (const auto& b : *bindings) {
          if (auto binding_tbl = b.as_table()) {
            auto action_str = (*binding_tbl)["action"].as_string();
            auto hotkey = (*binding_tbl)["hotkey"].as_string();
            if (action_str && hotkey) {
              auto action = string_to_hotkey_action(action_str->get());
              if (action) {
                options.keyboardOptions.bindings.push_back({*action, hotkey->get()});
              }
            }
          }
        }
      }
    }

    // Parse gap section
    if (auto gap = tbl["gap"].as_table()) {
      if (auto horizontal = (*gap)["horizontal"].as_floating_point()) {
        options.gapOptions.horizontal = static_cast<float>(horizontal->get());
      }
      if (auto vertical = (*gap)["vertical"].as_floating_point()) {
        options.gapOptions.vertical = static_cast<float>(vertical->get());
      }
    }

    // Validate gap values - negative values not allowed
    if (options.gapOptions.horizontal < 0) {
      spdlog::error("Invalid gap.horizontal value ({}): must be non-negative. Using default.",
                    options.gapOptions.horizontal);
      options.gapOptions.horizontal = kDefaultGapHorizontal;
    }

    if (options.gapOptions.vertical < 0) {
      spdlog::error("Invalid gap.vertical value ({}): must be non-negative. Using default.",
                    options.gapOptions.vertical);
      options.gapOptions.vertical = kDefaultGapVertical;
    }

    // Parse render section
    if (auto render = tbl["render"].as_table()) {
      auto parseColor = [](const toml::array* arr) -> std::optional<overlay::Color> {
        if (!arr || arr->size() != 4) {
          return std::nullopt;
        }
        auto r = (*arr)[0].as_integer();
        auto g = (*arr)[1].as_integer();
        auto b = (*arr)[2].as_integer();
        auto a = (*arr)[3].as_integer();
        if (!r || !g || !b || !a) {
          return std::nullopt;
        }
        auto rv = r->get(), gv = g->get(), bv = b->get(), av = a->get();
        if (rv < 0 || rv > 255 || gv < 0 || gv > 255 || bv < 0 || bv > 255 || av < 0 || av > 255) {
          return std::nullopt;
        }
        return overlay::Color{static_cast<uint8_t>(rv), static_cast<uint8_t>(gv),
                              static_cast<uint8_t>(bv), static_cast<uint8_t>(av)};
      };

      if (auto color = parseColor((*render)["normal_color"].as_array())) {
        options.visualizationOptions.normalColor = *color;
      } else if ((*render)["normal_color"]) {
        spdlog::error("Invalid normal_color: values must be 0-255. Using default.");
      }
      if (auto color = parseColor((*render)["selected_color"].as_array())) {
        options.visualizationOptions.selectedColor = *color;
      } else if ((*render)["selected_color"]) {
        spdlog::error("Invalid selected_color: values must be 0-255. Using default.");
      }
      if (auto color = parseColor((*render)["stored_color"].as_array())) {
        options.visualizationOptions.storedColor = *color;
      } else if ((*render)["stored_color"]) {
        spdlog::error("Invalid stored_color: values must be 0-255. Using default.");
      }
      if (auto borderWidth = (*render)["border_width"].as_floating_point()) {
        options.visualizationOptions.borderWidth = static_cast<float>(borderWidth->get());
      }
      if (auto toastFontSize = (*render)["toast_font_size"].as_floating_point()) {
        options.visualizationOptions.toastFontSize = static_cast<float>(toastFontSize->get());
      }
      if (auto toastDurationMs = (*render)["toast_duration_ms"].as_integer()) {
        options.visualizationOptions.toastDurationMs = static_cast<int>(toastDurationMs->get());
      }
    }

    // Validate toast duration - negative values not allowed
    if (options.visualizationOptions.toastDurationMs < 0) {
      spdlog::error("Invalid toast_duration_ms value ({}): must be non-negative. Using default.",
                    options.visualizationOptions.toastDurationMs);
      options.visualizationOptions.toastDurationMs = 2000;
    }

    return ReadResult{true, "", options};
  } catch (const toml::parse_error& e) {
    return ReadResult{false, std::string("TOML parse error: ") + e.what(), {}};
  } catch (const std::exception& e) {
    return ReadResult{false, std::string("Error reading TOML: ") + e.what(), {}};
  }
}

GlobalOptionsProvider::GlobalOptionsProvider(std::optional<std::filesystem::path> path)
    : configPath(std::move(path)), options(get_default_global_options()), lastModified{} {
  if (configPath.has_value() && std::filesystem::exists(*configPath)) {
    auto result = read_options_toml(*configPath);
    if (result.success) {
      options = result.options;
      lastModified = std::filesystem::last_write_time(*configPath);
    } else {
      spdlog::error("Failed to load config: {}", result.error);
    }
  }
}

bool GlobalOptionsProvider::refresh() {
  if (!configPath.has_value()) {
    return false; // No file to monitor
  }
  if (!std::filesystem::exists(*configPath)) {
    return false; // File doesn't exist (yet)
  }

  auto currentModified = std::filesystem::last_write_time(*configPath);
  if (currentModified == lastModified) {
    return false; // No change
  }

  auto result = read_options_toml(*configPath);
  if (result.success) {
    options = result.options;
    lastModified = currentModified;
    spdlog::info("Config reloaded from: {}", configPath->string());
    return true;
  }
  spdlog::error("Failed to reload config: {}", result.error);
  return false;
}

} // namespace wintiler
