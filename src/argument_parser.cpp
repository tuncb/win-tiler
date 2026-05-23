#include "argument_parser.h"

#include <iostream>

namespace wintiler {

namespace {

std::optional<LogLevel> parse_log_level(const std::string& level) {
  if (level == "trace")
    return LogLevel::Trace;
  if (level == "debug")
    return LogLevel::Debug;
  if (level == "info")
    return LogLevel::Info;
  if (level == "warn")
    return LogLevel::Warn;
  if (level == "err")
    return LogLevel::Err;
  if (level == "off")
    return LogLevel::Off;
  return std::nullopt;
}

ParseResult make_error(const std::string& error) {
  ParseResult result;
  result.success = false;
  result.error = error;
  return result;
}

ParseResult make_success(ParsedArgs args) {
  ParseResult result;
  result.success = true;
  result.args = std::move(args);
  return result;
}

} // namespace

ParseResult parse_args(int argc, char* argv[]) {
  ParsedArgs args;
  int i = 1;

  // Parse options first (--option value)
  while (i < argc) {
    std::string arg = argv[i];

    // Check for help flags
    if (arg == "--help" || arg == "-h") {
      args.command = HelpCommand{};
      return make_success(args);
    }

    // Check for version flag
    if (arg == "--version" || arg == "-v") {
      args.command = VersionCommand{};
      return make_success(args);
    }

    if (arg == "--monitor-info") {
      args.command = MonitorInfoCommand{};
      return make_success(args);
    }

    // Check if it's an option (starts with --)
    if (arg.rfind("--", 0) == 0) {
      std::string option_name = arg.substr(2);

      if (option_name == "logmode") {
        if (i + 1 >= argc) {
          return make_error("--logmode requires a value");
        }
        ++i;
        std::string value = argv[i];
        auto level = parse_log_level(value);
        if (!level) {
          return make_error("Invalid log level: " + value +
                            ". Valid values: trace, debug, info, warn, err, off");
        }
        args.options.log_level = level;
      } else if (option_name == "log-file") {
        if (i + 1 >= argc) {
          return make_error("--log-file requires a filepath");
        }
        ++i;
        args.options.log_file_path = argv[i];
      } else if (option_name == "config") {
        if (i + 1 >= argc) {
          return make_error("--config requires a filepath");
        }
        ++i;
        args.options.config_path = argv[i];
      } else if (option_name == "perf-stats") {
        args.options.perf_stats = true;
      } else {
        return make_error("Unknown option: --" + option_name);
      }
      ++i;
      continue;
    }

    // Not an option, must be a command
    break;
  }

  // Parse command if present
  if (i < argc) {
    std::string cmd = argv[i];
    ++i;

    if (cmd == "version") {
      args.command = VersionCommand{};
    } else if (cmd == "loop") {
      args.command = LoopCommand{};
    } else if (cmd == "monitor-info") {
      args.command = MonitorInfoCommand{};
    } else if (cmd == "track-windows") {
      args.command = TrackWindowsCommand{};
    } else if (cmd == "init-config") {
      InitConfigCommand init_cmd;
      if (i < argc && argv[i][0] != '-') {
        // Optional filepath argument provided
        init_cmd.filepath = argv[i];
        ++i;
      }
      args.command = init_cmd;
    } else if (cmd == "startup") {
      if (i >= argc) {
        return make_error("startup requires an action: enable, disable, or status");
      }

      std::string action = argv[i];
      ++i;

      StartupCommand startup_cmd;
      if (action == "enable") {
        startup_cmd.action = StartupAction::Enable;
      } else if (action == "disable") {
        startup_cmd.action = StartupAction::Disable;
      } else if (action == "status") {
        startup_cmd.action = StartupAction::Status;
      } else {
        return make_error("Unknown startup action: " + action +
                          ". Valid actions: enable, disable, status");
      }

      if (i < argc) {
        return make_error("startup does not accept extra arguments");
      }

      args.command = startup_cmd;
    } else {
      return make_error("Unknown command: " + cmd);
    }
  } else {
    args.command = LoopCommand{};
  }

  return make_success(args);
}

void print_usage() {
  std::cout << "Usage: win-tiler [options] [command] [command-args]\n"
            << "\n"
            << "Options:\n"
            << "  --help, -h              Show this help message\n"
            << "  --version, -v           Show version information\n"
            << "  --monitor-info          Show monitor information and exit\n"
            << "  --logmode <level>       Set log level (trace, debug, info, warn, err, off)\n"
            << "  --log-file <filepath>   Write logs to a file instead of stdout\n"
            << "  --config <filepath>     Load configuration from a TOML file\n"
            << "  --perf-stats            Print periodic loop performance summaries\n"
            << "\n"
            << "Commands:\n"
            << "  version                 Show version information\n"
            << "  loop                    Run in loop mode (hotkey-driven, default)\n"
            << "  monitor-info            Show monitor information and exit\n"
            << "  track-windows           Track and log windows per monitor in a loop\n"
            << "  init-config [filepath]  Create default configuration TOML file\n"
            << "                          (defaults to win-tiler.toml next to executable)\n"
            << "  startup <action>        Manage startup registration for the current user\n"
            << "                          (actions: enable, disable, status)\n"
            << "\n"
            << "Examples:\n"
            << "  win-tiler --logmode debug loop\n"
            << "  win-tiler --monitor-info\n"
            << "  win-tiler init-config config.toml\n"
            << "  win-tiler --config config.toml loop\n"
            << "  win-tiler startup enable\n";
}

} // namespace wintiler
