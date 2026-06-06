#include "save_layout.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <fstream>
#include <memory>
#include <sstream>
#include <string_view>
#include <system_error>
#include <toml++/toml.hpp>

namespace wintiler {

namespace {

struct TextLine {
  std::string text;
};

struct RuleBlock {
  size_t start_line = 0;
  size_t end_line = 0;
  size_t window_count = 0;
};

struct MonitorProfileBlock {
  size_t start_line = 0;
  size_t end_line = 0;
  std::optional<std::string> device_name;
  std::optional<size_t> index;
  std::optional<bool> primary;
  std::vector<RuleBlock> rules;
};

[[nodiscard]] std::string trim(std::string_view value) {
  size_t start = 0;
  while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0) {
    ++start;
  }

  size_t end = value.size();
  while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
    --end;
  }

  return std::string(value.substr(start, end - start));
}

[[nodiscard]] bool starts_with(std::string_view value, std::string_view prefix) {
  return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

[[nodiscard]] bool is_table_header(std::string_view trimmed_line) {
  return starts_with(trimmed_line, "[") && trimmed_line.find(']') != std::string_view::npos;
}

[[nodiscard]] std::vector<TextLine> split_lines(std::string_view text) {
  std::vector<TextLine> lines;
  size_t start = 0;
  while (start < text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string_view::npos) {
      lines.push_back({std::string(text.substr(start))});
      return lines;
    }

    size_t line_end = end;
    if (line_end > start && text[line_end - 1] == '\r') {
      --line_end;
    }
    lines.push_back({std::string(text.substr(start, line_end - start))});
    start = end + 1;
  }

  if (!text.empty() && text.back() == '\n') {
    lines.push_back({""});
  }
  return lines;
}

[[nodiscard]] std::string join_lines(const std::vector<TextLine>& lines) {
  std::ostringstream stream;
  for (size_t i = 0; i < lines.size(); ++i) {
    if (i > 0) {
      stream << '\n';
    }
    stream << lines[i].text;
  }
  return stream.str();
}

[[nodiscard]] std::optional<std::string> parse_toml_string_after_equals(std::string_view line,
                                                                        std::string_view key) {
  size_t key_pos = line.find(key);
  if (key_pos == std::string_view::npos) {
    return std::nullopt;
  }

  size_t equals_pos = line.find('=', key_pos + key.size());
  if (equals_pos == std::string_view::npos) {
    return std::nullopt;
  }

  size_t quote_pos = line.find('"', equals_pos + 1);
  if (quote_pos == std::string_view::npos) {
    return std::nullopt;
  }

  std::string result;
  bool escaping = false;
  for (size_t i = quote_pos + 1; i < line.size(); ++i) {
    char ch = line[i];
    if (escaping) {
      switch (ch) {
      case 'b':
        result.push_back('\b');
        break;
      case 't':
        result.push_back('\t');
        break;
      case 'n':
        result.push_back('\n');
        break;
      case 'f':
        result.push_back('\f');
        break;
      case 'r':
        result.push_back('\r');
        break;
      case '"':
      case '\\':
        result.push_back(ch);
        break;
      default:
        result.push_back(ch);
        break;
      }
      escaping = false;
      continue;
    }

    if (ch == '\\') {
      escaping = true;
      continue;
    }
    if (ch == '"') {
      return result;
    }
    result.push_back(ch);
  }

  return std::nullopt;
}

[[nodiscard]] std::optional<size_t> parse_size_after_equals(std::string_view line,
                                                            std::string_view key) {
  size_t key_pos = line.find(key);
  if (key_pos == std::string_view::npos) {
    return std::nullopt;
  }

  size_t equals_pos = line.find('=', key_pos + key.size());
  if (equals_pos == std::string_view::npos) {
    return std::nullopt;
  }

  size_t value_start = equals_pos + 1;
  while (value_start < line.size() &&
         std::isspace(static_cast<unsigned char>(line[value_start])) != 0) {
    ++value_start;
  }

  size_t value_end = value_start;
  while (value_end < line.size() && std::isdigit(static_cast<unsigned char>(line[value_end])) != 0) {
    ++value_end;
  }
  if (value_start == value_end) {
    return std::nullopt;
  }

  size_t result = 0;
  auto parse_result =
      std::from_chars(line.data() + value_start, line.data() + value_end, result);
  if (parse_result.ec != std::errc{}) {
    return std::nullopt;
  }
  return result;
}

[[nodiscard]] std::optional<bool> parse_bool_after_equals(std::string_view line,
                                                          std::string_view key) {
  size_t key_pos = line.find(key);
  if (key_pos == std::string_view::npos) {
    return std::nullopt;
  }

  size_t equals_pos = line.find('=', key_pos + key.size());
  if (equals_pos == std::string_view::npos) {
    return std::nullopt;
  }

  std::string value = trim(line.substr(equals_pos + 1));
  if (starts_with(value, "true")) {
    return true;
  }
  if (starts_with(value, "false")) {
    return false;
  }
  return std::nullopt;
}

[[nodiscard]] size_t find_profile_end(const std::vector<TextLine>& lines, size_t start_line) {
  for (size_t i = start_line + 1; i < lines.size(); ++i) {
    std::string line = trim(lines[i].text);
    if (line == "[[monitor_profiles]]") {
      return i;
    }
    if (is_table_header(line) && !starts_with(line, "[monitor_profiles") &&
        !starts_with(line, "[[monitor_profiles")) {
      return i;
    }
  }
  return lines.size();
}

[[nodiscard]] size_t find_rule_end(const std::vector<TextLine>& lines, size_t start_line,
                                   size_t profile_end) {
  for (size_t i = start_line + 1; i < profile_end; ++i) {
    std::string line = trim(lines[i].text);
    if (!is_table_header(line)) {
      continue;
    }
    if (line == "[[monitor_profiles.layout.rules]]" || line == "[[monitor_profiles]]") {
      return i;
    }
    if (!starts_with(line, "[monitor_profiles.layout.rules.tree")) {
      return i;
    }
  }
  return profile_end;
}

[[nodiscard]] std::vector<MonitorProfileBlock> scan_monitor_profiles(
    const std::vector<TextLine>& lines) {
  std::vector<MonitorProfileBlock> profiles;

  for (size_t i = 0; i < lines.size(); ++i) {
    if (trim(lines[i].text) != "[[monitor_profiles]]") {
      continue;
    }

    MonitorProfileBlock profile;
    profile.start_line = i;
    profile.end_line = find_profile_end(lines, i);

    for (size_t line_index = profile.start_line + 1; line_index < profile.end_line; ++line_index) {
      std::string line = trim(lines[line_index].text);
      if (line == "[[monitor_profiles.layout.rules]]") {
        RuleBlock rule;
        rule.start_line = line_index;
        rule.end_line = find_rule_end(lines, line_index, profile.end_line);
        for (size_t rule_line = rule.start_line + 1; rule_line < rule.end_line; ++rule_line) {
          auto window_count = parse_size_after_equals(lines[rule_line].text, "window_count");
          if (window_count.has_value()) {
            rule.window_count = *window_count;
            break;
          }
        }
        profile.rules.push_back(rule);
        line_index = rule.end_line == 0 ? line_index : rule.end_line - 1;
        continue;
      }

      if (!profile.device_name.has_value()) {
        profile.device_name = parse_toml_string_after_equals(line, "device_name");
      }
      if (!profile.index.has_value()) {
        profile.index = parse_size_after_equals(line, "index");
      }
      if (!profile.primary.has_value()) {
        profile.primary = parse_bool_after_equals(line, "primary");
      }
    }

    profiles.push_back(profile);
    i = profile.end_line == 0 ? i : profile.end_line - 1;
  }

  return profiles;
}

[[nodiscard]] LayoutSplitDir to_layout_split_dir(ctrl::SplitDir split_dir) {
  switch (split_dir) {
  case ctrl::SplitDir::Vertical:
    return LayoutSplitDir::Vertical;
  case ctrl::SplitDir::Horizontal:
    return LayoutSplitDir::Horizontal;
  }
  return LayoutSplitDir::Vertical;
}

[[nodiscard]] std::string layout_split_dir_to_toml(LayoutSplitDir split_dir) {
  switch (split_dir) {
  case LayoutSplitDir::Vertical:
    return "vertical";
  case LayoutSplitDir::Horizontal:
    return "horizontal";
  }
  return "vertical";
}

[[nodiscard]] size_t count_cluster_leaves_with_ids(const ctrl::Cluster& cluster) {
  size_t count = 0;
  for (int index = 0; index < static_cast<int>(cluster.tree.size()); ++index) {
    if (cluster.tree.is_leaf(index) && cluster.tree[index].leaf_id.has_value()) {
      ++count;
    }
  }
  return count;
}

[[nodiscard]] tl::expected<std::shared_ptr<LayoutTreeNode>, std::string>
build_layout_child_from_cluster_node(const ctrl::Cluster& cluster, int node_index);

[[nodiscard]] tl::expected<LayoutTreeNode, std::string>
build_layout_node_from_cluster_node(const ctrl::Cluster& cluster, int node_index) {
  if (!cluster.tree.is_valid_index(node_index)) {
    return tl::unexpected("Invalid cluster tree node");
  }
  if (cluster.tree.is_leaf(node_index)) {
    return tl::unexpected("Cannot save a single window layout rule");
  }

  const auto& source_node = cluster.tree.node(node_index);
  if (!source_node.first_child.has_value() || !source_node.second_child.has_value()) {
    return tl::unexpected("Cannot save incomplete cluster tree");
  }

  LayoutTreeNode result;
  result.split_dir = to_layout_split_dir(source_node.data.split_dir);
  result.split_ratio = source_node.data.split_ratio;

  auto first = build_layout_child_from_cluster_node(cluster, *source_node.first_child);
  if (!first.has_value()) {
    return tl::unexpected(first.error());
  }
  result.first = *first;

  auto second = build_layout_child_from_cluster_node(cluster, *source_node.second_child);
  if (!second.has_value()) {
    return tl::unexpected(second.error());
  }
  result.second = *second;

  return result;
}

[[nodiscard]] tl::expected<std::shared_ptr<LayoutTreeNode>, std::string>
build_layout_child_from_cluster_node(const ctrl::Cluster& cluster, int node_index) {
  if (!cluster.tree.is_valid_index(node_index)) {
    return tl::unexpected("Invalid cluster tree child node");
  }
  if (cluster.tree.is_leaf(node_index)) {
    return std::shared_ptr<LayoutTreeNode>{};
  }

  auto node = build_layout_node_from_cluster_node(cluster, node_index);
  if (!node.has_value()) {
    return tl::unexpected(node.error());
  }
  return std::make_shared<LayoutTreeNode>(std::move(*node));
}

void append_layout_node_lines(const LayoutTreeNode& node, const std::string& table_path,
                              std::vector<TextLine>& lines) {
  lines.push_back({"[" + table_path + "]"});
  lines.push_back({"split = \"" + layout_split_dir_to_toml(node.split_dir) + "\""});
  lines.push_back({"ratio = " + std::to_string(node.split_ratio)});

  if (!node.first) {
    lines.push_back({"first = \"window\""});
  }
  if (!node.second) {
    lines.push_back({"second = \"window\""});
  }

  if (node.first) {
    lines.push_back({""});
    append_layout_node_lines(*node.first, table_path + ".first", lines);
  }
  if (node.second) {
    lines.push_back({""});
    append_layout_node_lines(*node.second, table_path + ".second", lines);
  }
}

[[nodiscard]] std::vector<TextLine> make_rule_lines(const LayoutRule& rule) {
  std::vector<TextLine> lines;
  lines.push_back({"[[monitor_profiles.layout.rules]]"});
  lines.push_back({"window_count = " + std::to_string(rule.window_count)});
  lines.push_back({""});
  append_layout_node_lines(rule.tree, "monitor_profiles.layout.rules.tree", lines);
  return lines;
}

[[nodiscard]] std::string toml_escape_basic_string(std::string_view value) {
  std::string result;
  for (char ch : value) {
    switch (ch) {
    case '\\':
      result += "\\\\";
      break;
    case '"':
      result += "\\\"";
      break;
    case '\n':
      result += "\\n";
      break;
    case '\r':
      result += "\\r";
      break;
    case '\t':
      result += "\\t";
      break;
    default:
      result.push_back(ch);
      break;
    }
  }
  return result;
}

[[nodiscard]] std::string monitor_display_name(const winapi::MonitorInfo& monitor,
                                               size_t monitor_index) {
  if (!monitor.deviceName.empty()) {
    return monitor.deviceName;
  }
  return "Monitor " + std::to_string(monitor_index);
}

[[nodiscard]] bool profile_matches_monitor(const MonitorProfileBlock& profile,
                                           const winapi::MonitorInfo& monitor,
                                           size_t monitor_index) {
  if (profile.device_name.has_value() && *profile.device_name != monitor.deviceName) {
    return false;
  }
  if (profile.index.has_value() && *profile.index != monitor_index) {
    return false;
  }
  if (profile.primary.has_value() && *profile.primary != monitor.isPrimary) {
    return false;
  }
  return profile.device_name.has_value() || profile.index.has_value() || profile.primary.has_value();
}

[[nodiscard]] bool profile_matches_only_target_monitor(
    const MonitorProfileBlock& profile, const std::vector<winapi::MonitorInfo>& current_monitors,
    size_t target_monitor_index) {
  size_t match_count = 0;
  size_t matched_index = 0;
  for (size_t i = 0; i < current_monitors.size(); ++i) {
    if (profile_matches_monitor(profile, current_monitors[i], i)) {
      ++match_count;
      matched_index = i;
    }
  }
  return match_count == 1 && matched_index == target_monitor_index;
}

[[nodiscard]] std::optional<size_t> find_target_profile_index(
    const std::vector<MonitorProfileBlock>& profiles,
    const std::vector<winapi::MonitorInfo>& current_monitors,
    const MonitorLayoutRuleUpdate& update) {
  std::optional<size_t> result;
  if (!update.monitor.deviceName.empty()) {
    for (size_t i = 0; i < profiles.size(); ++i) {
      if (profiles[i].device_name.has_value() &&
          *profiles[i].device_name == update.monitor.deviceName) {
        result = i;
      }
    }
    if (result.has_value()) {
      return result;
    }
  }

  for (size_t i = 0; i < profiles.size(); ++i) {
    if (profile_matches_only_target_monitor(profiles[i], current_monitors, update.monitor_index)) {
      result = i;
    }
  }

  return result;
}

void append_new_profile(std::vector<TextLine>& lines, const MonitorLayoutRuleUpdate& update) {
  if (!lines.empty() && !lines.back().text.empty()) {
    lines.push_back({""});
  }

  lines.push_back({"[[monitor_profiles]]"});
  lines.push_back({"name = \"" +
                   toml_escape_basic_string(monitor_display_name(update.monitor,
                                                                 update.monitor_index)) +
                   "\""});
  if (!update.monitor.deviceName.empty()) {
    lines.push_back({"match = { device_name = \"" +
                     toml_escape_basic_string(update.monitor.deviceName) + "\" }"});
  } else {
    lines.push_back({"match = { index = " + std::to_string(update.monitor_index) + " }"});
  }
  lines.push_back({""});

  auto rule_lines = make_rule_lines(update.rule);
  lines.insert(lines.end(), rule_lines.begin(), rule_lines.end());
}

[[nodiscard]] tl::expected<void, std::string>
apply_update_to_lines(std::vector<TextLine>& lines,
                      const std::vector<winapi::MonitorInfo>& current_monitors,
                      const MonitorLayoutRuleUpdate& update) {
  auto profiles = scan_monitor_profiles(lines);
  auto target_profile = find_target_profile_index(profiles, current_monitors, update);
  if (!target_profile.has_value()) {
    append_new_profile(lines, update);
    return {};
  }

  const MonitorProfileBlock& profile = profiles[*target_profile];
  auto rule_lines = make_rule_lines(update.rule);
  for (auto rule_it = profile.rules.rbegin(); rule_it != profile.rules.rend(); ++rule_it) {
    if (rule_it->window_count == update.rule.window_count) {
      auto erase_begin = lines.begin() + static_cast<std::ptrdiff_t>(rule_it->start_line);
      auto erase_end = lines.begin() + static_cast<std::ptrdiff_t>(rule_it->end_line);
      lines.erase(erase_begin, erase_end);
      lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(rule_it->start_line),
                   rule_lines.begin(), rule_lines.end());
      return {};
    }
  }

  size_t insert_line = profile.end_line;
  if (insert_line > 0 && insert_line <= lines.size() && !lines[insert_line - 1].text.empty()) {
    rule_lines.insert(rule_lines.begin(), {""});
  }
  lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(insert_line), rule_lines.begin(),
               rule_lines.end());
  return {};
}

[[nodiscard]] tl::expected<std::string, std::string>
read_file_text(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return tl::unexpected("Failed to open config file for reading: " + path.string());
  }

  std::ostringstream stream;
  stream << file.rdbuf();
  return stream.str();
}

[[nodiscard]] tl::expected<void, std::string> write_text_atomically(
    const std::filesystem::path& path, const std::string& text) {
  std::filesystem::path temp_path = path;
  temp_path += ".tmp";

  {
    std::ofstream file(temp_path, std::ios::binary | std::ios::trunc);
    if (!file) {
      return tl::unexpected("Failed to open temporary config file for writing: " +
                            temp_path.string());
    }
    file << text;
    if (!file) {
      return tl::unexpected("Failed to write temporary config file: " + temp_path.string());
    }
  }

  std::error_code error;
  std::filesystem::rename(temp_path, path, error);
  if (!error) {
    return {};
  }

  error.clear();
  std::filesystem::copy_file(temp_path, path, std::filesystem::copy_options::overwrite_existing,
                             error);
  if (error) {
    return tl::unexpected("Failed to replace config file: " + error.message());
  }
  std::filesystem::remove(temp_path, error);
  return {};
}

} // namespace

tl::expected<LayoutRule, std::string> build_layout_rule_from_cluster(
    const ctrl::Cluster& cluster) {
  size_t window_count = count_cluster_leaves_with_ids(cluster);
  if (window_count < 2) {
    return tl::unexpected("Need at least 2 tiled windows");
  }
  if (cluster.tree.empty()) {
    return tl::unexpected("Cannot save an empty layout");
  }

  auto tree = build_layout_node_from_cluster_node(cluster, 0);
  if (!tree.has_value()) {
    return tl::unexpected(tree.error());
  }

  LayoutRule rule;
  rule.window_count = window_count;
  rule.tree = std::move(*tree);
  if (count_layout_windows(rule.tree) != window_count) {
    return tl::unexpected("Saved layout tree leaf count does not match window count");
  }
  return rule;
}

tl::expected<SaveLayoutConfigUpdateResult, std::string> save_monitor_layout_rules_to_config(
    const std::filesystem::path& config_path,
    const std::vector<winapi::MonitorInfo>& current_monitors,
    const std::vector<MonitorLayoutRuleUpdate>& updates) {
  if (updates.empty()) {
    return SaveLayoutConfigUpdateResult{};
  }

  auto text = read_file_text(config_path);
  if (!text.has_value()) {
    return tl::unexpected(text.error());
  }

  try {
    auto parsed = toml::parse(*text);
    if (parsed.empty() && !text->empty()) {
      return tl::unexpected("Config file parsed as an empty TOML document unexpectedly");
    }
  } catch (const toml::parse_error& e) {
    return tl::unexpected(std::string("TOML parse error: ") + e.what());
  }

  std::vector<TextLine> lines = split_lines(*text);
  for (const auto& update : updates) {
    if (update.monitor_index >= current_monitors.size()) {
      return tl::unexpected("Monitor index is out of range while saving layout");
    }

    auto apply_result = apply_update_to_lines(lines, current_monitors, update);
    if (!apply_result.has_value()) {
      return tl::unexpected(apply_result.error());
    }
  }

  std::string updated_text = join_lines(lines);
  try {
    auto parsed = toml::parse(updated_text);
    if (parsed.empty() && !updated_text.empty()) {
      return tl::unexpected("Updated config parsed as an empty TOML document unexpectedly");
    }
  } catch (const toml::parse_error& e) {
    return tl::unexpected(std::string("Updated TOML parse error: ") + e.what());
  }

  auto write_result = write_text_atomically(config_path, updated_text);
  if (!write_result.has_value()) {
    return tl::unexpected(write_result.error());
  }

  return SaveLayoutConfigUpdateResult{updates.size()};
}

} // namespace wintiler
