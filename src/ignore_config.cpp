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
#include <windows.h>

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
write_text_atomically(const std::filesystem::path& path, const std::string& text,
                      std::optional<std::string_view> expected = std::nullopt,
                      bool create_new = false) {
  static std::atomic<unsigned long> sequence{0};
  std::filesystem::path temp_path = path;
  temp_path +=
      ".tmp." + std::to_string(GetCurrentProcessId()) + "." + std::to_string(sequence.fetch_add(1));
  HANDLE file = CreateFileW(temp_path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return tl::unexpected("Cannot create temporary config: " + std::to_string(GetLastError()));
  }
  DWORD written = 0;
  bool wrote = text.size() <= (std::numeric_limits<DWORD>::max)() &&
               WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) &&
               written == text.size() && FlushFileBuffers(file);
  DWORD write_error = wrote ? ERROR_SUCCESS : GetLastError();
  if (!CloseHandle(file)) {
    spdlog::error("Failed to close temporary config handle: {}", GetLastError());
    wrote = false;
  }
  auto cleanup = [&] {
    if (!DeleteFileW(temp_path.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND) {
      spdlog::error("Failed to remove temporary config '{}': {}", temp_path.string(),
                    GetLastError());
    }
  };
  if (!wrote) {
    cleanup();
    return tl::unexpected("Could not write config: " + std::to_string(write_error));
  }
  if (create_new) {
    // Publish the complete first rule in one operation. Without REPLACE_EXISTING,
    // a config created by another process wins and remains untouched.
    if (!MoveFileExW(temp_path.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH)) {
      const DWORD error = GetLastError();
      cleanup();
      return tl::unexpected("Could not create config (already exists or not writable): " +
                            std::to_string(error));
    }
    return {};
  }
  if (expected.has_value()) {
    auto current = read_file_text(path);
    if (!current || *current != *expected) {
      cleanup();
      return tl::unexpected("The config changed while saving. Refresh and try again.");
    }
  }
  // Never fall back to copying over the live file: a failed copy can leave a
  // partially written configuration. ReplaceFile also retains the original ACL.
  if (!ReplaceFileW(path.c_str(), temp_path.c_str(), nullptr, 0, nullptr, nullptr)) {
    DWORD error = GetLastError();
    cleanup();
    return tl::unexpected("Could not replace config (read-only or in use): " +
                          std::to_string(error));
  }
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

bool ignore_rules_equal(const IgnoreRule& left, const IgnoreRule& right) {
  if (left.kind != right.kind)
    return false;
  switch (left.kind) {
  case IgnoreRuleKind::Process:
  case IgnoreRuleKind::ChildProcess:
    return iequals(left.process, right.process);
  case IgnoreRuleKind::Title:
    return left.title == right.title;
  case IgnoreRuleKind::ProcessTitlePair:
    return iequals(left.process, right.process) && iequals(left.title, right.title);
  }
  return false;
}

bool ignore_rule_has_values(const IgnoreRule& rule) {
  switch (rule.kind) {
  case IgnoreRuleKind::Process:
  case IgnoreRuleKind::ChildProcess:
    return !rule.process.empty();
  case IgnoreRuleKind::Title:
    return !rule.title.empty();
  case IgnoreRuleKind::ProcessTitlePair:
    return !rule.process.empty() && !rule.title.empty();
  }
  return false;
}

bool ignore_rule_matches(const IgnoreRule& rule, std::string_view process, std::string_view title,
                         bool child_or_owned) {
  if (rule.kind == IgnoreRuleKind::ChildProcess && !child_or_owned)
    return false;
  return ignore_rules_equal(rule, IgnoreRule{rule.kind, std::string(process), std::string(title)});
}

const char* ignore_rule_kind_name(IgnoreRuleKind kind) {
  switch (kind) {
  case IgnoreRuleKind::Process:
    return "Entire app";
  case IgnoreRuleKind::Title:
    return "Exact title";
  case IgnoreRuleKind::ProcessTitlePair:
    return "App + title";
  case IgnoreRuleKind::ChildProcess:
    return "Owned windows";
  }
  return "Unknown rule";
}

namespace {
constexpr std::array<IgnoreRuleKind, 4> rule_kinds{IgnoreRuleKind::Process, IgnoreRuleKind::Title,
                                                   IgnoreRuleKind::ProcessTitlePair,
                                                   IgnoreRuleKind::ChildProcess};

std::string_view rule_key(IgnoreRuleKind kind) {
  switch (kind) {
  case IgnoreRuleKind::Process:
    return "processes";
  case IgnoreRuleKind::Title:
    return "window_titles";
  case IgnoreRuleKind::ProcessTitlePair:
    return "process_title_pairs";
  case IgnoreRuleKind::ChildProcess:
    return "ignore_children_of_processes";
  }
  return "";
}

tl::expected<std::vector<IgnoreRule>, std::string> rules_from_table(const toml::table& parsed) {
  std::vector<IgnoreRule> rules;
  const auto* ignore = parsed["ignore"].as_table();
  if (parsed.contains("ignore") && !ignore) {
    return tl::unexpected("The ignore section must be a TOML table.");
  }
  if (!ignore)
    return rules;
  for (auto kind : rule_kinds) {
    const auto* node = ignore->get(rule_key(kind));
    if (!node)
      continue;
    const auto* array = node->as_array();
    if (!array)
      return tl::unexpected(std::string(rule_key(kind)) + " must be an array.");
    for (const auto& entry : *array) {
      IgnoreRule rule{kind, {}, {}};
      if (kind == IgnoreRuleKind::ProcessTitlePair) {
        const auto* table = entry.as_table();
        if (!table || table->size() != 2 || !(*table)["process"].is_string() ||
            !(*table)["title"].is_string()) {
          return tl::unexpected("Each process/title rule needs only process and title strings. "
                                "Edit unsupported entries in the config file.");
        }
        rule.process = *(*table)["process"].value<std::string>();
        rule.title = *(*table)["title"].value<std::string>();
      } else {
        if (!entry.is_string()) {
          return tl::unexpected(std::string(rule_key(kind)) + " must contain only strings.");
        }
        if (kind == IgnoreRuleKind::Title)
          rule.title = entry.as_string()->get();
        else
          rule.process = entry.as_string()->get();
      }
      rules.push_back(std::move(rule));
    }
  }
  return rules;
}

// toml++ source columns count Unicode codepoints, not UTF-8 bytes.
size_t source_offset(std::string_view text, toml::source_position position) {
  size_t offset = 0;
  for (size_t line = 1; line < position.line && offset < text.size(); ++line) {
    auto end = text.find('\n', offset);
    offset = end == std::string_view::npos ? text.size() : end + 1;
  }
  for (size_t column = 1; column < position.column && offset < text.size(); ++column) {
    ++offset;
    while (offset < text.size() && (static_cast<unsigned char>(text[offset]) & 0xc0) == 0x80) {
      ++offset;
    }
  }
  return offset;
}

tl::expected<std::string, std::string> rewrite_rule_array(std::string text, IgnoreRuleKind kind,
                                                          const std::vector<IgnoreRule>& rules) {
  auto parsed = parse_existing_config(text);
  if (!parsed)
    return tl::unexpected(parsed.error());
  std::vector<std::string> values;
  std::vector<std::pair<std::string, std::string>> pairs;
  for (const auto& rule : rules) {
    if (rule.kind != kind)
      continue;
    if (kind == IgnoreRuleKind::ProcessTitlePair)
      pairs.emplace_back(rule.process, rule.title);
    else
      values.push_back(kind == IgnoreRuleKind::Title ? rule.title : rule.process);
  }
  const std::string line = kind == IgnoreRuleKind::ProcessTitlePair
                               ? make_process_title_pairs_line(pairs)
                               : make_string_array_line(rule_key(kind), values);
  const std::string value = line.substr(line.find('=') + 2);
  auto* ignore = (*parsed)["ignore"].as_table();
  if (!ignore) {
    text += "\n[ignore]\n" + line + "\n";
  } else if (const auto* node = ignore->get(rule_key(kind))) {
    const size_t begin = source_offset(text, node->source().begin);
    const size_t end = source_offset(text, node->source().end);
    if (begin >= end || text.compare(begin, 2, "[[") == 0) {
      return tl::unexpected("This rule uses array-of-table syntax. Edit it in the config file.");
    }
    text.replace(begin, end - begin, value);
  } else if (ignore->is_inline()) {
    const size_t end = source_offset(text, ignore->source().end);
    if (end == 0 || text[end - 1] != '}')
      return tl::unexpected("Cannot locate inline ignore table.");
    text.insert(end - 1, (ignore->empty() ? " " : ", ") + line + " ");
  } else {
    const size_t begin = source_offset(text, ignore->source().begin);
    if (begin < text.size() && text[begin] == '[') {
      const size_t end = text.find('\n', begin);
      if (end == std::string::npos)
        text += "\n" + line + "\n";
      else
        text.insert(end + 1, line + "\n");
    } else {
      // A table defined by dotted keys has no [ignore] header.
      text.insert(0, "ignore." + line + "\n");
    }
  }
  auto validation = validate_toml_text(text, "Updated config");
  if (!validation)
    return tl::unexpected(validation.error());
  return text;
}
} // namespace

tl::expected<std::vector<IgnoreRule>, std::string>
read_user_ignore_rules(const std::filesystem::path& config_path) {
  auto text = read_file_text(config_path);
  if (!text)
    return tl::unexpected(text.error());
  auto parsed = parse_existing_config(*text);
  if (!parsed)
    return tl::unexpected(parsed.error());
  return rules_from_table(*parsed);
}

tl::expected<IgnoreRuleEdit, std::string>
edit_ignore_rules(const std::filesystem::path& config_path, const std::vector<IgnoreRule>& remove,
                  const std::vector<IgnoreRule>& add, bool create_if_missing) {
  for (const auto& rule : add) {
    if (!ignore_rule_has_values(rule))
      return tl::unexpected("The rule needs an app name or title.");
  }
  std::error_code error;
  const bool create_new = create_if_missing && remove.empty() && !add.empty() &&
                          !std::filesystem::exists(config_path, error) && !error;
  auto original = create_new
      ? tl::expected<std::string, std::string>(
            "# win-tiler configuration. Unspecified options use defaults.\n[ignore]\n")
      : read_file_text(config_path);
  if (!original)
    return tl::unexpected(original.error());
  auto parsed = parse_existing_config(*original);
  if (!parsed)
    return tl::unexpected(parsed.error());
  auto rules = rules_from_table(*parsed);
  if (!rules)
    return tl::unexpected(rules.error());
  for (const auto& rule : remove) {
    if (std::none_of(rules->begin(), rules->end(),
                     [&](const auto& r) { return ignore_rules_equal(r, rule); })) {
      return tl::unexpected("That rule was changed or removed elsewhere. Refresh and try again.");
    }
  }
  IgnoreRuleEdit edit;
  std::erase_if(*rules, [&](const auto& rule) {
    bool removing = std::any_of(remove.begin(), remove.end(),
                                [&](const auto& r) { return ignore_rules_equal(r, rule); });
    if (removing)
      edit.removed.push_back(rule);
    return removing;
  });
  for (const auto& rule : add) {
    if (std::none_of(rules->begin(), rules->end(),
                     [&](const auto& r) { return ignore_rules_equal(r, rule); })) {
      rules->push_back(rule);
      edit.added.push_back(rule);
    }
  }
  if (!edit.changed())
    return edit;
  if (edit.removed.size() == 1 && edit.added.size() == 1 &&
      ignore_rules_equal(edit.removed.front(), edit.added.front()))
    return IgnoreRuleEdit{};
  std::string updated = *original;
  for (auto kind : rule_kinds) {
    auto has_kind = [kind](const auto& r) { return r.kind == kind; };
    if (!std::any_of(edit.removed.begin(), edit.removed.end(), has_kind) &&
        !std::any_of(edit.added.begin(), edit.added.end(), has_kind))
      continue;
    auto rewritten = rewrite_rule_array(std::move(updated), kind, *rules);
    if (!rewritten)
      return tl::unexpected(rewritten.error());
    updated = std::move(*rewritten);
  }
  auto saved = write_text_atomically(config_path, updated, *original, create_new);
  if (!saved)
    return tl::unexpected(saved.error());
  return edit;
}

tl::expected<void, std::string> create_ignore_config(const std::filesystem::path& path) {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return tl::unexpected("Could not create config: " + std::to_string(GetLastError()));
  }
  constexpr char contents[] =
      "# win-tiler configuration. Unspecified options use defaults.\n[ignore]\n";
  DWORD written = 0;
  bool ok = WriteFile(file, contents, sizeof(contents) - 1, &written, nullptr) &&
            written == sizeof(contents) - 1 && FlushFileBuffers(file);
  if (!CloseHandle(file)) {
    spdlog::error("Failed to close new config file: {}", GetLastError());
    ok = false;
  }
  if (!ok) {
    if (!DeleteFileW(path.c_str()))
      spdlog::error("Failed to remove incomplete config: {}", GetLastError());
    return tl::unexpected("Could not write the new config file.");
  }
  return {};
}

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
