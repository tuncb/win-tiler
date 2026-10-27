#include "config_file.h"

#include <spdlog/spdlog.h>
#include <windows.h>

#include <atomic>
#include <fstream>
#include <limits>
#include <sstream>

namespace wintiler {

[[nodiscard]] tl::expected<std::string, std::string>
read_config_text(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return tl::unexpected("Failed to open config file for reading: " + path.string());
  }

  std::ostringstream stream;
  stream << file.rdbuf();
  if (file.bad())
    return tl::unexpected("Failed to read config file: " + path.string());
  return stream.str();
}

[[nodiscard]] tl::expected<void, std::string>
write_config_text_atomically(const std::filesystem::path& path, const std::string& text,
                             std::optional<std::string_view> expected, bool create_new) {
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
    auto current = read_config_text(path);
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

// toml++ source columns count Unicode codepoints, not UTF-8 bytes.
size_t config_source_offset(std::string_view text, toml::source_position position) {
  size_t offset = text.starts_with("\xef\xbb\xbf") ? 3 : 0;
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

tl::expected<std::string, std::string>
rewrite_config_values(std::string text, const std::vector<ConfigValueEdit>& edits) {
  try {
    for (const auto& edit : edits) {
      if (edit.key.empty())
        return tl::unexpected("A config setting needs a key.");
      auto parsed = toml::parse(text);
      auto* table = &parsed;
      size_t depth = 0;
      std::vector<std::pair<toml::table*, size_t>> ancestors{{table, depth}};
      while (depth + 1 < edit.key.size()) {
        auto* node = table->get(edit.key[depth]);
        if (!node)
          break;
        table = node->as_table();
        if (!table)
          return tl::unexpected("The setting's parent must be a TOML table.");
        ++depth;
        ancestors.emplace_back(table, depth);
      }
      const auto* value = depth + 1 == edit.key.size() ? table->get(edit.key.back()) : nullptr;
      if (value) {
        if (value->is_table() || value->is_array())
          return tl::unexpected("This setting must be a scalar. Edit it in the config file.");
        const size_t begin = config_source_offset(text, value->source().begin);
        const size_t end = config_source_offset(text, value->source().end);
        if (begin >= end || end > text.size())
          return tl::unexpected("Cannot locate the config setting.");
        text.replace(begin, end - begin, edit.value);
      } else {
        const std::string newline = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
        auto key_from = [&](size_t first) {
          std::string key;
          for (size_t i = first; i < edit.key.size(); ++i) {
            if (!key.empty())
              key += '.';
            key += edit.key[i];
          }
          return key + " = " + edit.value;
        };
        bool inserted = false;
        // Dotted keys create implicit tables, whose source can point at a child
        // header or key. Try enclosing lexical tables and verify the absolute
        // key before accepting an insertion. This also handles dotted keys
        // nested inside an inline table without rewriting that entire table.
        for (auto ancestor = ancestors.rbegin(); ancestor != ancestors.rend(); ++ancestor) {
          const auto* container = ancestor->first;
          const size_t key_depth = ancestor->second;
          std::string candidate = text;
          if (container->is_inline()) {
            const size_t end = config_source_offset(text, container->source().end);
            if (end == 0 || text[end - 1] != '}')
              continue;
            candidate.insert(end - 1,
                             (container->empty() ? " " : ", ") + key_from(key_depth) + " ");
          } else if (container == &parsed) {
            const size_t begin_of_file = text.starts_with("\xef\xbb\xbf") ? 3 : 0;
            candidate.insert(begin_of_file, key_from(0) + newline);
          } else {
            const size_t begin = config_source_offset(text, container->source().begin);
            if (begin >= text.size() || text[begin] != '[')
              continue;
            const size_t end = text.find('\n', begin);
            if (end == std::string::npos)
              candidate += newline + key_from(key_depth) + newline;
            else
              candidate.insert(end + 1, key_from(key_depth) + newline);
          }
          try {
            const auto verified = toml::parse(candidate);
            const toml::node* saved = &verified;
            for (const auto key : edit.key)
              saved = saved && saved->is_table() ? saved->as_table()->get(key) : nullptr;
            if (!saved || !saved->is_value())
              continue;
            text = std::move(candidate);
            inserted = true;
            break;
          } catch (const toml::parse_error&) {
            // This was an implicit rather than lexical table; try its parent.
          }
        }
        if (!inserted)
          return tl::unexpected(
              "Cannot add this setting without rewriting its table. Edit it in the config file.");
      }
      // Reject ambiguous/unsupported representations before touching the live file.
      auto verified = toml::parse(text);
      const toml::node* saved = &verified;
      for (const auto key : edit.key) {
        saved = saved && saved->is_table() ? saved->as_table()->get(key) : nullptr;
      }
      if (!saved || !saved->is_value())
        return tl::unexpected("Could not update the requested setting.");
    }
    return text;
  } catch (const toml::parse_error& error) {
    return tl::unexpected(std::string("Config parse error: ") + error.what());
  }
}

} // namespace wintiler
