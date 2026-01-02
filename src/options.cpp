#include "options.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <fstream>
#include <magic_enum/magic_enum.hpp>
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
  case HotkeyAction::CycleSplitMode:
    return "CycleSplitMode";
  case HotkeyAction::StoreCell:
    return "StoreCell";
  case HotkeyAction::ClearStored:
    return "ClearStored";
  case HotkeyAction::Exchange:
    return "Exchange";
  case HotkeyAction::Move:
    return "Move";
  case HotkeyAction::SplitIncrease:
    return "SplitIncrease";
  case HotkeyAction::SplitDecrease:
    return "SplitDecrease";
  case HotkeyAction::ExchangeSiblings:
    return "ExchangeSiblings";
  case HotkeyAction::ToggleZen:
    return "ToggleZen";
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
  if (str == "CycleSplitMode")
    return HotkeyAction::CycleSplitMode;
  if (str == "StoreCell")
    return HotkeyAction::StoreCell;
  if (str == "ClearStored")
    return HotkeyAction::ClearStored;
  if (str == "Exchange")
    return HotkeyAction::Exchange;
  if (str == "Move")
    return HotkeyAction::Move;
  if (str == "SplitIncrease")
    return HotkeyAction::SplitIncrease;
  if (str == "SplitDecrease")
    return HotkeyAction::SplitDecrease;
  if (str == "ExchangeSiblings")
    return HotkeyAction::ExchangeSiblings;
  if (str == "ToggleZen")
    return HotkeyAction::ToggleZen;
  return std::nullopt;
}

std::string get_default_hotkey(HotkeyAction action) {
  switch (action) {
  case HotkeyAction::NavigateLeft:
    return "super+shift+h";
  case HotkeyAction::NavigateDown:
    return "super+shift+j";
  case HotkeyAction::NavigateUp:
    return "super+shift+k";
  case HotkeyAction::NavigateRight:
    return "super+shift+l";
  case HotkeyAction::ToggleSplit:
    return "super+shift+y";
  case HotkeyAction::Exit:
    return "super+shift+escape";
  case HotkeyAction::CycleSplitMode:
    return "super+shift+;";
  case HotkeyAction::StoreCell:
    return "super+shift+[";
  case HotkeyAction::ClearStored:
    return "super+shift+]";
  case HotkeyAction::Exchange:
    return "super+shift+,";
  case HotkeyAction::Move:
    return "super+shift+.";
  case HotkeyAction::SplitIncrease:
    return "super+shift+pageup";
  case HotkeyAction::SplitDecrease:
    return "super+shift+pagedown";
  case HotkeyAction::ExchangeSiblings:
    return "super+shift+e";
  case HotkeyAction::ToggleZen:
    return "super+shift+'";
  }
  return "";
}

// Helper to read a numeric value, accepting both float and integer TOML types
template <typename T>
std::optional<T> get_number(const toml::node_view<toml::node>& node) {
  if (auto fp = node.as_floating_point()) {
    return static_cast<T>(fp->get());
  }
  if (auto integer = node.as_integer()) {
    return static_cast<T>(integer->get());
  }
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
  options.small_window_barrier =
      SmallWindowBarrier{kDefaultSmallWindowBarrierWidth, kDefaultSmallWindowBarrierHeight};
  return options;
}

GlobalOptions get_default_global_options() {
  GlobalOptions options;
  options.ignoreOptions = get_default_ignore_options();
  for (auto action : magic_enum::enum_values<HotkeyAction>()) {
    options.keyboardOptions.bindings.push_back({action, get_default_hotkey(action)});
  }
  return options;
}

tl::expected<void, std::string> write_options_toml(const GlobalOptions& options,
                                                   const std::filesystem::path& filepath) {
  try {
    toml::table root;

    // Build ignore section
    toml::table ignore;

    // Write merge flags
    ignore.insert("merge_processes_with_defaults", options.ignoreOptions.merge_processes);
    ignore.insert("merge_window_titles_with_defaults", options.ignoreOptions.merge_window_titles);
    ignore.insert("merge_process_title_pairs_with_defaults",
                  options.ignoreOptions.merge_process_title_pairs);

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

    // Build loop section
    toml::table loop;
    loop.insert("interval_ms", options.loopOptions.intervalMs);
    root.insert("loop", loop);

    // Build visualization section with nested render
    toml::table visualization;
    toml::table render;
    auto colorToArray = [](const overlay::Color& c) {
      toml::array arr;
      arr.push_back(static_cast<int64_t>(c.r));
      arr.push_back(static_cast<int64_t>(c.g));
      arr.push_back(static_cast<int64_t>(c.b));
      arr.push_back(static_cast<int64_t>(c.a));
      return arr;
    };
    const auto& ro = options.visualizationOptions.renderOptions;
    render.insert("normal_color", colorToArray(ro.normal_color));
    render.insert("selected_color", colorToArray(ro.selected_color));
    render.insert("stored_color", colorToArray(ro.stored_color));
    render.insert("border_width", ro.border_width);
    render.insert("toast_font_size", ro.toast_font_size);
    render.insert("zen_percentage", ro.zen_percentage);
    visualization.insert("render", render);
    visualization.insert("toast_duration_ms", options.visualizationOptions.toastDurationMs);
    root.insert("visualization", visualization);

    // Write to file
    std::ofstream file(filepath);
    if (!file) {
      return tl::unexpected("Failed to open file for writing: " + filepath.string());
    }
    file << root;
    return {};
  } catch (const std::exception& e) {
    return tl::unexpected(std::string("Error writing TOML: ") + e.what());
  }
}

// Reads options from a TOML file at the given path.
// All fields are optional; missing fields will use default values.
// All fields are validated; invalid values will be replaced with defaults.
// Float values should also accept integer values from TOML files.
tl::expected<GlobalOptions, std::string> read_options_toml(const std::filesystem::path& filepath) {
  try {
    auto tbl = toml::parse_file(filepath.string());
    GlobalOptions options;

    // Parse ignore section
    auto defaultIgnore = get_default_ignore_options();

    // Read merge flags (default to true if not present)
    bool mergeProcesses = true;
    bool mergeWindowTitles = true;
    bool mergeProcessTitlePairs = true;

    // Temporary storage for user values
    std::vector<std::string> userProcesses;
    std::vector<std::string> userWindowTitles;
    std::vector<std::pair<std::string, std::string>> userProcessTitlePairs;

    if (auto ignore = tbl["ignore"].as_table()) {
      // Read merge flags
      if (auto flag = (*ignore)["merge_processes_with_defaults"].as_boolean()) {
        mergeProcesses = flag->get();
      }
      if (auto flag = (*ignore)["merge_window_titles_with_defaults"].as_boolean()) {
        mergeWindowTitles = flag->get();
      }
      if (auto flag = (*ignore)["merge_process_title_pairs_with_defaults"].as_boolean()) {
        mergeProcessTitlePairs = flag->get();
      }

      if (auto processes = (*ignore)["processes"].as_array()) {
        for (const auto& p : *processes) {
          if (auto str = p.as_string()) {
            userProcesses.push_back(str->get());
          }
        }
      }

      if (auto titles = (*ignore)["window_titles"].as_array()) {
        for (const auto& t : *titles) {
          if (auto str = t.as_string()) {
            userWindowTitles.push_back(str->get());
          }
        }
      }

      if (auto pairs = (*ignore)["process_title_pairs"].as_array()) {
        for (const auto& pair : *pairs) {
          if (auto tbl = pair.as_table()) {
            auto process = (*tbl)["process"].as_string();
            auto title = (*tbl)["title"].as_string();
            if (process && title) {
              userProcessTitlePairs.emplace_back(process->get(), title->get());
            }
          }
        }
      }

      if (auto barrier = (*ignore)["small_window_barrier"].as_table()) {
        auto width = (*barrier)["width"].as_integer();
        auto height = (*barrier)["height"].as_integer();
        if (width && height) {
          int w = static_cast<int>(width->get());
          int h = static_cast<int>(height->get());
          if (w < 0 || h < 0) {
            spdlog::error(
                "Invalid small_window_barrier: dimensions must be non-negative. Using default.");
            options.ignoreOptions.small_window_barrier = SmallWindowBarrier{
                kDefaultSmallWindowBarrierWidth, kDefaultSmallWindowBarrierHeight};
          } else {
            options.ignoreOptions.small_window_barrier = SmallWindowBarrier{w, h};
          }
        }
      }
    }

    // Store merge flags in options
    options.ignoreOptions.merge_processes = mergeProcesses;
    options.ignoreOptions.merge_window_titles = mergeWindowTitles;
    options.ignoreOptions.merge_process_title_pairs = mergeProcessTitlePairs;

    // Apply merge logic for processes
    if (mergeProcesses) {
      options.ignoreOptions.ignored_processes = defaultIgnore.ignored_processes;
      for (const auto& proc : userProcesses) {
        if (std::find(options.ignoreOptions.ignored_processes.begin(),
                      options.ignoreOptions.ignored_processes.end(),
                      proc) == options.ignoreOptions.ignored_processes.end()) {
          options.ignoreOptions.ignored_processes.push_back(proc);
        }
      }
    } else {
      options.ignoreOptions.ignored_processes = std::move(userProcesses);
    }

    // Apply merge logic for window titles
    if (mergeWindowTitles) {
      options.ignoreOptions.ignored_window_titles = defaultIgnore.ignored_window_titles;
      for (const auto& title : userWindowTitles) {
        if (std::find(options.ignoreOptions.ignored_window_titles.begin(),
                      options.ignoreOptions.ignored_window_titles.end(),
                      title) == options.ignoreOptions.ignored_window_titles.end()) {
          options.ignoreOptions.ignored_window_titles.push_back(title);
        }
      }
    } else {
      options.ignoreOptions.ignored_window_titles = std::move(userWindowTitles);
    }

    // Apply merge logic for process/title pairs
    if (mergeProcessTitlePairs) {
      options.ignoreOptions.ignored_process_title_pairs = defaultIgnore.ignored_process_title_pairs;
      for (const auto& pair : userProcessTitlePairs) {
        if (std::find(options.ignoreOptions.ignored_process_title_pairs.begin(),
                      options.ignoreOptions.ignored_process_title_pairs.end(),
                      pair) == options.ignoreOptions.ignored_process_title_pairs.end()) {
          options.ignoreOptions.ignored_process_title_pairs.push_back(pair);
        }
      }
    } else {
      options.ignoreOptions.ignored_process_title_pairs = std::move(userProcessTitlePairs);
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

    // Merge with default bindings - add defaults for any missing actions
    auto defaultOptions = get_default_global_options();
    for (const auto& defaultBinding : defaultOptions.keyboardOptions.bindings) {
      bool found = false;
      for (const auto& binding : options.keyboardOptions.bindings) {
        if (binding.action == defaultBinding.action) {
          found = true;
          break;
        }
      }
      if (!found) {
        options.keyboardOptions.bindings.push_back(defaultBinding);
      }
    }

    // Parse gap section
    if (auto gap = tbl["gap"].as_table()) {
      if (auto horizontal = get_number<float>((*gap)["horizontal"])) {
        options.gapOptions.horizontal = *horizontal;
      }
      if (auto vertical = get_number<float>((*gap)["vertical"])) {
        options.gapOptions.vertical = *vertical;
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

    // Parse loop section
    if (auto loop = tbl["loop"].as_table()) {
      if (auto intervalMs = (*loop)["interval_ms"].as_integer()) {
        options.loopOptions.intervalMs = static_cast<int>(intervalMs->get());
      }
    }

    // Validate loop interval - negative values not allowed
    if (options.loopOptions.intervalMs < 0) {
      spdlog::error("Invalid loop.interval_ms value ({}): must be non-negative. Using default.",
                    options.loopOptions.intervalMs);
      options.loopOptions.intervalMs = kDefaultLoopIntervalMs;
    }

    // Parse visualization section with nested render
    if (auto visualization = tbl["visualization"].as_table()) {
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

      // Parse nested render section
      if (auto render = (*visualization)["render"].as_table()) {
        auto& ro = options.visualizationOptions.renderOptions;
        if (auto color = parseColor((*render)["normal_color"].as_array())) {
          ro.normal_color = *color;
        } else if ((*render)["normal_color"]) {
          spdlog::error("Invalid normal_color: values must be 0-255. Using default.");
        }
        if (auto color = parseColor((*render)["selected_color"].as_array())) {
          ro.selected_color = *color;
        } else if ((*render)["selected_color"]) {
          spdlog::error("Invalid selected_color: values must be 0-255. Using default.");
        }
        if (auto color = parseColor((*render)["stored_color"].as_array())) {
          ro.stored_color = *color;
        } else if ((*render)["stored_color"]) {
          spdlog::error("Invalid stored_color: values must be 0-255. Using default.");
        }
        if (auto borderWidth = get_number<float>((*render)["border_width"])) {
          ro.border_width = *borderWidth;
        }
        if (auto toastFontSize = get_number<float>((*render)["toast_font_size"])) {
          ro.toast_font_size = *toastFontSize;
        }
        if (auto zenPercentage = get_number<float>((*render)["zen_percentage"])) {
          ro.zen_percentage = *zenPercentage;
        }
      }

      // Parse toast_duration_ms from visualization level
      if (auto toastDurationMs = (*visualization)["toast_duration_ms"].as_integer()) {
        options.visualizationOptions.toastDurationMs = static_cast<int>(toastDurationMs->get());
      }
    }

    // Validate toast duration - negative values not allowed
    if (options.visualizationOptions.toastDurationMs < 0) {
      spdlog::error("Invalid toast_duration_ms value ({}): must be non-negative. Using default.",
                    options.visualizationOptions.toastDurationMs);
      options.visualizationOptions.toastDurationMs = kDefaultToastDurationMs;
    }

    auto& ro = options.visualizationOptions.renderOptions;

    // Validate border width - negative values not allowed
    if (ro.border_width < 0) {
      spdlog::error("Invalid border_width value ({}): must be non-negative. Using default.",
                    ro.border_width);
      ro.border_width = kDefaultBorderWidth;
    }

    // Validate toast font size - must be positive
    if (ro.toast_font_size < 1.0f) {
      spdlog::error("Invalid toast_font_size value ({}): must be >= 1.0. Using default.",
                    ro.toast_font_size);
      ro.toast_font_size = kDefaultToastFontSize;
    }

    // Validate zen percentage - clamp to 0.1-1.0 range
    if (ro.zen_percentage < 0.1f) {
      spdlog::error("Invalid zen_percentage value ({}): must be >= 0.1. Using 0.1.",
                    ro.zen_percentage);
      ro.zen_percentage = 0.1f;
    } else if (ro.zen_percentage > 1.0f) {
      spdlog::error("Invalid zen_percentage value ({}): must be <= 1.0. Using 1.0.",
                    ro.zen_percentage);
      ro.zen_percentage = 1.0f;
    }

    return options;
  } catch (const toml::parse_error& e) {
    return tl::unexpected(std::string("TOML parse error: ") + e.what());
  } catch (const std::exception& e) {
    return tl::unexpected(std::string("Error reading TOML: ") + e.what());
  }
}

GlobalOptionsProvider::GlobalOptionsProvider(std::optional<std::filesystem::path> path)
    : configPath(std::move(path)), options(get_default_global_options()), lastModified{} {
  if (configPath.has_value() && std::filesystem::exists(*configPath)) {
    auto result = read_options_toml(*configPath);
    if (result.has_value()) {
      options = result.value();
      lastModified = std::filesystem::last_write_time(*configPath);
    } else {
      spdlog::error("Failed to load config: {}", result.error());
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
  if (result.has_value()) {
    options = result.value();
    lastModified = currentModified;
    spdlog::info("Config reloaded from: {}", configPath->string());
    return true;
  }
  spdlog::error("Failed to reload config: {}", result.error());
  return false;
}

} // namespace wintiler
