#include "ignore_config.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <toml++/toml.hpp>
#include <utility>
#include <vector>

namespace wintiler {
namespace {

struct TextLine {
  std::string text;
};

struct TableRange {
  size_t begin = 0;
  size_t end = 0;
  bool found = false;
};

struct KeyRange {
  size_t begin = 0;
  size_t end = 0;
  bool found = false;
};

[[nodiscard]] std::string trim_copy(std::string_view text) {
  size_t begin = 0;
  while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
    ++begin;
  }
  size_t end = text.size();
  while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
    --end;
  }
  return std::string(text.substr(begin, end - begin));
}

[[nodiscard]] bool iequals(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) {
    return false;
  }
  return std::equal(a.begin(), a.end(), b.begin(), [](char left, char right) {
    return std::tolower(static_cast<unsigned char>(left)) ==
           std::tolower(static_cast<unsigned char>(right));
  });
}

[[nodiscard]] bool pairs_match(const std::pair<std::string, std::string>& pair,
                               std::string_view process, std::string_view title) {
  return iequals(pair.first, process) && iequals(pair.second, title);
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

[[nodiscard]] tl::expected<void, std::string>
write_text_atomically(const std::filesystem::path& path, const std::string& text) {
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

[[nodiscard]] std::vector<TextLine> split_lines(std::string_view text) {
  std::vector<TextLine> lines;
  size_t start = 0;
  while (start < text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string_view::npos) {
      lines.push_back({std::string(text.substr(start))});
      break;
    }

    size_t length = end - start;
    if (length > 0 && text[start + length - 1] == '\r') {
      --length;
    }
    lines.push_back({std::string(text.substr(start, length))});
    start = end + 1;
  }
  return lines;
}

[[nodiscard]] std::string join_lines(const std::vector<TextLine>& lines) {
  std::string result;
  for (size_t i = 0; i < lines.size(); ++i) {
    if (i > 0) {
      result += "\n";
    }
    result += lines[i].text;
  }
  if (!result.empty()) {
    result += "\n";
  }
  return result;
}

[[nodiscard]] bool is_table_header(std::string_view line) {
  std::string trimmed = trim_copy(line);
  return trimmed.size() >= 3 && trimmed.front() == '[' && trimmed.back() == ']';
}

[[nodiscard]] bool is_ignore_table_header(std::string_view line) {
  return trim_copy(line) == "[ignore]";
}

[[nodiscard]] TableRange find_ignore_table(const std::vector<TextLine>& lines) {
  for (size_t i = 0; i < lines.size(); ++i) {
    if (!is_ignore_table_header(lines[i].text)) {
      continue;
    }

    size_t end = lines.size();
    for (size_t j = i + 1; j < lines.size(); ++j) {
      if (is_table_header(lines[j].text)) {
        end = j;
        break;
      }
    }
    return TableRange{i, end, true};
  }
  return {};
}

[[nodiscard]] bool line_starts_with_key(std::string_view line, std::string_view key) {
  std::string trimmed = trim_copy(line);
  if (trimmed.size() <= key.size() || trimmed.substr(0, key.size()) != key) {
    return false;
  }

  size_t pos = key.size();
  while (pos < trimmed.size() && std::isspace(static_cast<unsigned char>(trimmed[pos])) != 0) {
    ++pos;
  }
  return pos < trimmed.size() && trimmed[pos] == '=';
}

[[nodiscard]] int bracket_delta(std::string_view line) {
  bool in_string = false;
  bool escaped = false;
  int delta = 0;
  for (char character : line) {
    if (in_string) {
      if (escaped) {
        escaped = false;
      } else if (character == '\\') {
        escaped = true;
      } else if (character == '"') {
        in_string = false;
      }
      continue;
    }

    if (character == '#') {
      break;
    }
    if (character == '"') {
      in_string = true;
    } else if (character == '[') {
      ++delta;
    } else if (character == ']') {
      --delta;
    }
  }
  return delta;
}

[[nodiscard]] KeyRange find_ignore_key(const std::vector<TextLine>& lines, const TableRange& table,
                                       std::string_view key) {
  for (size_t i = table.begin + 1; i < table.end; ++i) {
    if (!line_starts_with_key(lines[i].text, key)) {
      continue;
    }

    int depth = bracket_delta(lines[i].text);
    size_t end = i + 1;
    while (depth > 0 && end < table.end) {
      depth += bracket_delta(lines[end].text);
      ++end;
    }
    return KeyRange{i, end, true};
  }
  return {};
}

[[nodiscard]] std::string make_string_array_line(
    std::string_view key, const std::vector<std::string>& values) {
  toml::array array;
  for (const auto& value : values) {
    array.push_back(value);
  }

  std::ostringstream stream;
  stream << array;
  return std::string(key) + " = " + stream.str();
}

[[nodiscard]] std::string make_process_title_pairs_line(
    const std::vector<std::pair<std::string, std::string>>& pairs) {
  toml::array array;
  for (const auto& [process, title] : pairs) {
    toml::table pair;
    pair.insert("process", process);
    pair.insert("title", title);
    array.push_back(std::move(pair));
  }

  std::ostringstream stream;
  stream << array;
  return "process_title_pairs = " + stream.str();
}

[[nodiscard]] std::vector<std::string> read_user_string_list(const toml::table& parsed,
                                                             std::string_view key) {
  std::vector<std::string> values;
  const auto* ignore = parsed["ignore"].as_table();
  if (ignore == nullptr) {
    return values;
  }

  const auto* array = (*ignore)[key].as_array();
  if (array == nullptr) {
    return values;
  }

  for (const auto& entry : *array) {
    const auto* value = entry.as_string();
    if (value != nullptr) {
      values.push_back(value->get());
    }
  }
  return values;
}

[[nodiscard]] std::vector<std::pair<std::string, std::string>>
read_user_process_title_pairs(const toml::table& parsed) {
  std::vector<std::pair<std::string, std::string>> pairs;
  const auto* ignore = parsed["ignore"].as_table();
  if (ignore == nullptr) {
    return pairs;
  }

  const auto* array = (*ignore)["process_title_pairs"].as_array();
  if (array == nullptr) {
    return pairs;
  }

  for (const auto& entry : *array) {
    const auto* pair_table = entry.as_table();
    if (pair_table == nullptr) {
      continue;
    }
    const auto* process = (*pair_table)["process"].as_string();
    const auto* title = (*pair_table)["title"].as_string();
    if (process != nullptr && title != nullptr) {
      pairs.emplace_back(process->get(), title->get());
    }
  }
  return pairs;
}

[[nodiscard]] tl::expected<void, std::string>
validate_toml_text(std::string_view text, const char* error_prefix) {
  try {
    auto parsed = toml::parse(text);
    if (parsed.empty() && !text.empty()) {
      return tl::unexpected(std::string(error_prefix) +
                            " parsed as an empty TOML document unexpectedly");
    }
  } catch (const toml::parse_error& e) {
    return tl::unexpected(std::string(error_prefix) + " parse error: " + e.what());
  }
  return {};
}

[[nodiscard]] tl::expected<toml::table, std::string> parse_existing_config(
    const std::string& text) {
  try {
    auto parsed = toml::parse(text);
    if (parsed.empty() && !text.empty()) {
      return tl::unexpected("Config file parsed as an empty TOML document unexpectedly");
    }
    return parsed;
  } catch (const toml::parse_error& e) {
    return tl::unexpected(std::string("TOML parse error: ") + e.what());
  }
}

[[nodiscard]] std::vector<TextLine> apply_ignore_key_line_to_lines(std::vector<TextLine> lines,
                                                                   std::string_view key,
                                                                   const std::string& line) {
  TableRange table = find_ignore_table(lines);

  if (!table.found) {
    if (!lines.empty() && !lines.back().text.empty()) {
      lines.push_back({""});
    }
    lines.push_back({"[ignore]"});
    lines.push_back({line});
    return lines;
  }

  KeyRange key_range = find_ignore_key(lines, table, key);
  if (key_range.found) {
    lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(key_range.begin),
                lines.begin() + static_cast<std::ptrdiff_t>(key_range.end));
    lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(key_range.begin), {line});
    return lines;
  }

  size_t insert_line = table.end;
  lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(insert_line), {line});
  return lines;
}

[[nodiscard]] std::vector<TextLine>
apply_process_title_pairs_to_lines(std::vector<TextLine> lines,
                                   const std::vector<std::pair<std::string, std::string>>& pairs) {
  const std::string line = make_process_title_pairs_line(pairs);
  return apply_ignore_key_line_to_lines(std::move(lines), "process_title_pairs", line);
}

enum class PairMutation {
  Add,
  Remove,
};

enum class ListMutation {
  Add,
  Remove,
};

[[nodiscard]] bool string_value_matches(std::string_view left, std::string_view right,
                                        bool case_insensitive) {
  if (case_insensitive) {
    return iequals(left, right);
  }
  return left == right;
}

[[nodiscard]] tl::expected<IgnoreStringListUpdateResult, std::string>
update_ignore_string_list_config(const std::filesystem::path& config_path, std::string_view key,
                                 const std::string& value, bool case_insensitive,
                                 ListMutation mutation) {
  if (value.empty()) {
    return tl::unexpected("Ignore value must be non-empty");
  }

  auto text = read_file_text(config_path);
  if (!text.has_value()) {
    return tl::unexpected(text.error());
  }

  auto parsed = parse_existing_config(*text);
  if (!parsed.has_value()) {
    return tl::unexpected(parsed.error());
  }

  auto values = read_user_string_list(*parsed, key);
  bool changed = false;

  switch (mutation) {
  case ListMutation::Add:
    if (std::none_of(values.begin(), values.end(), [&](const auto& existing) {
          return string_value_matches(existing, value, case_insensitive);
        })) {
      values.push_back(value);
      changed = true;
    }
    break;
  case ListMutation::Remove: {
    const size_t old_size = values.size();
    values.erase(std::remove_if(values.begin(), values.end(),
                                [&](const auto& existing) {
                                  return string_value_matches(existing, value, case_insensitive);
                                }),
                 values.end());
    changed = values.size() != old_size;
    break;
  }
  }

  if (!changed) {
    return IgnoreStringListUpdateResult{false, values.size()};
  }

  std::vector<TextLine> lines = split_lines(*text);
  const std::string line = make_string_array_line(key, values);
  lines = apply_ignore_key_line_to_lines(std::move(lines), key, line);
  std::string updated_text = join_lines(lines);

  auto validation = validate_toml_text(updated_text, "Updated TOML");
  if (!validation.has_value()) {
    return tl::unexpected(validation.error());
  }

  auto write_result = write_text_atomically(config_path, updated_text);
  if (!write_result.has_value()) {
    return tl::unexpected(write_result.error());
  }

  return IgnoreStringListUpdateResult{true, values.size()};
}

[[nodiscard]] tl::expected<IgnoreProcessTitlePairUpdateResult, std::string>
update_ignore_process_title_pair_config(const std::filesystem::path& config_path,
                                        const std::string& process, const std::string& title,
                                        PairMutation mutation) {
  if (process.empty() || title.empty()) {
    return tl::unexpected("Process and title must both be non-empty");
  }

  auto text = read_file_text(config_path);
  if (!text.has_value()) {
    return tl::unexpected(text.error());
  }

  auto parsed = parse_existing_config(*text);
  if (!parsed.has_value()) {
    return tl::unexpected(parsed.error());
  }

  auto pairs = read_user_process_title_pairs(*parsed);
  bool changed = false;

  switch (mutation) {
  case PairMutation::Add:
    if (std::none_of(pairs.begin(), pairs.end(), [&](const auto& pair) {
          return pairs_match(pair, process, title);
        })) {
      pairs.emplace_back(process, title);
      changed = true;
    }
    break;
  case PairMutation::Remove: {
    const size_t old_size = pairs.size();
    pairs.erase(std::remove_if(pairs.begin(), pairs.end(),
                               [&](const auto& pair) { return pairs_match(pair, process, title); }),
                pairs.end());
    changed = pairs.size() != old_size;
    break;
  }
  }

  if (!changed) {
    return IgnoreProcessTitlePairUpdateResult{false, pairs.size()};
  }

  std::vector<TextLine> lines = split_lines(*text);
  lines = apply_process_title_pairs_to_lines(std::move(lines), pairs);
  std::string updated_text = join_lines(lines);

  auto validation = validate_toml_text(updated_text, "Updated TOML");
  if (!validation.has_value()) {
    return tl::unexpected(validation.error());
  }

  auto write_result = write_text_atomically(config_path, updated_text);
  if (!write_result.has_value()) {
    return tl::unexpected(write_result.error());
  }

  return IgnoreProcessTitlePairUpdateResult{true, pairs.size()};
}

} // namespace

tl::expected<IgnoreStringListUpdateResult, std::string>
add_ignore_process_to_config(const std::filesystem::path& config_path,
                             const std::string& process) {
  return update_ignore_string_list_config(config_path, "processes", process, true,
                                          ListMutation::Add);
}

tl::expected<IgnoreStringListUpdateResult, std::string>
remove_ignore_process_from_config(const std::filesystem::path& config_path,
                                  const std::string& process) {
  return update_ignore_string_list_config(config_path, "processes", process, true,
                                          ListMutation::Remove);
}

tl::expected<IgnoreStringListUpdateResult, std::string>
add_ignore_window_title_to_config(const std::filesystem::path& config_path,
                                  const std::string& title) {
  return update_ignore_string_list_config(config_path, "window_titles", title, false,
                                          ListMutation::Add);
}

tl::expected<IgnoreStringListUpdateResult, std::string>
remove_ignore_window_title_from_config(const std::filesystem::path& config_path,
                                       const std::string& title) {
  return update_ignore_string_list_config(config_path, "window_titles", title, false,
                                          ListMutation::Remove);
}

tl::expected<IgnoreStringListUpdateResult, std::string>
add_ignore_child_process_to_config(const std::filesystem::path& config_path,
                                   const std::string& process) {
  return update_ignore_string_list_config(config_path, "ignore_children_of_processes", process,
                                          true, ListMutation::Add);
}

tl::expected<IgnoreStringListUpdateResult, std::string>
remove_ignore_child_process_from_config(const std::filesystem::path& config_path,
                                        const std::string& process) {
  return update_ignore_string_list_config(config_path, "ignore_children_of_processes", process,
                                          true, ListMutation::Remove);
}

tl::expected<IgnoreProcessTitlePairUpdateResult, std::string>
add_ignore_process_title_pair_to_config(const std::filesystem::path& config_path,
                                        const std::string& process,
                                        const std::string& title) {
  return update_ignore_process_title_pair_config(config_path, process, title, PairMutation::Add);
}

tl::expected<IgnoreProcessTitlePairUpdateResult, std::string>
remove_ignore_process_title_pair_from_config(const std::filesystem::path& config_path,
                                             const std::string& process,
                                             const std::string& title) {
  return update_ignore_process_title_pair_config(config_path, process, title, PairMutation::Remove);
}

} // namespace wintiler
