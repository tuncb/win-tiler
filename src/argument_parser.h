#pragma once

#include <optional>
#include <string>
#include <variant>

namespace wintiler {

// ===== Command Structs =====
struct HelpCommand {};    // --help or -h
struct VersionCommand {}; // --version or version

struct LoopCommand {};

struct TrackWindowsCommand {};

enum class AgentTransport { Stdio };

struct AgentCommand {
  AgentTransport transport = AgentTransport::Stdio;
};

struct InitConfigCommand {
  std::optional<std::string> filepath; // Empty = use default (win-tiler.toml next to exe)
};

enum class StartupAction { Enable, Disable, Status };

struct StartupCommand {
  StartupAction action;
};

// Variant holding all possible commands
using Command = std::variant<HelpCommand, VersionCommand, LoopCommand, TrackWindowsCommand,
                             AgentCommand, InitConfigCommand, StartupCommand>;

// ===== CLI Options =====
enum class LogLevel { Trace, Debug, Info, Warn, Err, Off };

struct CliOptions {
  std::optional<LogLevel> log_level;      // --logmode <level>
  std::optional<std::string> config_path; // --config <filepath>
};

// ===== Parsed Arguments =====
struct ParsedArgs {
  CliOptions options;
  std::optional<Command> command; // Defaults to LoopCommand when no command is specified
};

// ===== Parser Result =====
struct ParseResult {
  bool success;
  std::string error; // Set if success == false
  ParsedArgs args;
};

// Parse command-line arguments
ParseResult parse_args(int argc, char* argv[]);

// Print usage information to stdout
void print_usage();

} // namespace wintiler
