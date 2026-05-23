#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "argument_parser.h"

namespace wintiler {

[[nodiscard]] bool command_should_attach_console(const Command& command, const CliOptions& options);
[[nodiscard]] bool command_should_default_to_temp_log_file(const Command& command,
                                                           const CliOptions& options,
                                                           bool console_attached);
[[nodiscard]] std::filesystem::path
get_default_temp_log_file_path(const std::filesystem::path& temp_directory);

[[nodiscard]] bool attach_parent_console();

void configure_default_console_logger();
void configure_default_null_logger();

[[nodiscard]] std::vector<std::string> wide_args_to_utf8(int argc, wchar_t* argv[]);

} // namespace wintiler
