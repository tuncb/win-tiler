#pragma once

#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace wintiler {

// ===== Command Structs =====
struct HelpCommand {};  // --help or -h

struct ApplyCommand {};

struct ApplyTestCommand {};

struct LoopCommand {};

struct LoopTestCommand {};

struct UiTestMonitorCommand {};

struct UiTestMultiCommand {
  struct ClusterDef {
    float x, y, width, height;
  };
  std::vector<ClusterDef> clusters;  // Empty = use defaults
};

// Variant holding all possible commands
using Command = std::variant<HelpCommand,
                             ApplyCommand,
                             ApplyTestCommand,
                             LoopCommand,
                             LoopTestCommand,
                             UiTestMonitorCommand,
                             UiTestMultiCommand>;

// ===== CLI Options =====
enum class LogLevel { Trace, Debug, Info, Warn, Err, Off };

struct CliOptions {
  std::optional<LogLevel> logLevel;  // --logmode <level>
  // Future options can be added here
};

// ===== Parsed Arguments =====
struct ParsedArgs {
  CliOptions options;
  std::optional<Command> command;  // nullopt if no command specified
};

// ===== Parser Result =====
struct ParseResult {
  bool success;
  std::string error;  // Set if success == false
  ParsedArgs args;
};

// Parse command-line arguments
ParseResult parseArgs(int argc, char* argv[]);

// Print usage information to stdout
void printUsage();

}  // namespace wintiler
