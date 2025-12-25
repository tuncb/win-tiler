#include "argument_parser.h"

#include <iostream>

namespace wintiler {

namespace {

std::optional<LogLevel> parseLogLevel(const std::string& level) {
  if (level == "trace") return LogLevel::Trace;
  if (level == "debug") return LogLevel::Debug;
  if (level == "info") return LogLevel::Info;
  if (level == "warn") return LogLevel::Warn;
  if (level == "err") return LogLevel::Err;
  if (level == "off") return LogLevel::Off;
  return std::nullopt;
}

ParseResult makeError(const std::string& error) {
  ParseResult result;
  result.success = false;
  result.error = error;
  return result;
}

ParseResult makeSuccess(ParsedArgs args) {
  ParseResult result;
  result.success = true;
  result.args = std::move(args);
  return result;
}

}  // namespace

ParseResult parseArgs(int argc, char* argv[]) {
  ParsedArgs args;
  int i = 1;

  // Parse options first (--option value)
  while (i < argc) {
    std::string arg = argv[i];

    // Check for help flags
    if (arg == "--help" || arg == "-h") {
      args.command = HelpCommand{};
      return makeSuccess(args);
    }

    // Check if it's an option (starts with --)
    if (arg.rfind("--", 0) == 0) {
      std::string optionName = arg.substr(2);

      if (optionName == "logmode") {
        if (i + 1 >= argc) {
          return makeError("--logmode requires a value");
        }
        ++i;
        std::string value = argv[i];
        auto level = parseLogLevel(value);
        if (!level) {
          return makeError("Invalid log level: " + value +
                           ". Valid values: trace, debug, info, warn, err, off");
        }
        args.options.logLevel = level;
      } else if (optionName == "config") {
        if (i + 1 >= argc) {
          return makeError("--config requires a filepath");
        }
        ++i;
        args.options.configPath = argv[i];
      } else {
        return makeError("Unknown option: --" + optionName);
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

    if (cmd == "apply") {
      args.command = ApplyCommand{};
    } else if (cmd == "apply-test") {
      args.command = ApplyTestCommand{};
    } else if (cmd == "loop") {
      args.command = LoopCommand{};
    } else if (cmd == "loop-test") {
      args.command = LoopTestCommand{};
    } else if (cmd == "ui-test-monitor") {
      args.command = UiTestMonitorCommand{};
    } else if (cmd == "ui-test-multi") {
      UiTestMultiCommand multiCmd;

      // Parse optional cluster definitions (groups of 4: x y w h)
      int remaining = argc - i;
      if (remaining > 0 && remaining % 4 != 0) {
        return makeError(
            "ui-test-multi requires 4 numbers per cluster (x y width height). "
            "Got " +
            std::to_string(remaining) + " arguments.");
      }

      while (i + 3 < argc) {
        try {
          UiTestMultiCommand::ClusterDef cluster;
          cluster.x = std::stof(argv[i]);
          cluster.y = std::stof(argv[i + 1]);
          cluster.width = std::stof(argv[i + 2]);
          cluster.height = std::stof(argv[i + 3]);
          multiCmd.clusters.push_back(cluster);
          i += 4;
        } catch (const std::exception&) {
          return makeError("Invalid number in ui-test-multi arguments");
        }
      }

      args.command = multiCmd;
    } else if (cmd == "track-windows") {
      args.command = TrackWindowsCommand{};
    } else if (cmd == "init-config") {
      InitConfigCommand initCmd;
      if (i < argc && argv[i][0] != '-') {
        // Optional filepath argument provided
        initCmd.filepath = argv[i];
        ++i;
      }
      args.command = initCmd;
    } else {
      return makeError("Unknown command: " + cmd);
    }
  }

  return makeSuccess(args);
}

void printUsage() {
  std::cout << "Usage: win-tiler [options] [command] [command-args]\n"
            << "\n"
            << "Options:\n"
            << "  --help, -h              Show this help message\n"
            << "  --logmode <level>       Set log level (trace, debug, info, warn, err, off)\n"
            << "  --config <filepath>     Load configuration from a TOML file\n"
            << "\n"
            << "Commands:\n"
            << "  apply                   Apply tiling to actual windows\n"
            << "  apply-test              Preview tiling layout in console\n"
            << "  loop                    Run in loop mode (hotkey-driven)\n"
            << "  loop-test               Run loop mode for testing\n"
            << "  ui-test-monitor         Launch UI visualizer with monitor data\n"
            << "  ui-test-multi [x y w h] Launch UI with custom cluster dimensions\n"
            << "                          (groups of 4 numbers, defaults to dual 1920x1080)\n"
            << "  track-windows           Track and log windows per monitor in a loop\n"
            << "  init-config [filepath]  Create default configuration TOML file\n"
            << "                          (defaults to win-tiler.toml next to executable)\n"
            << "\n"
            << "Examples:\n"
            << "  win-tiler --logmode debug loop\n"
            << "  win-tiler apply\n"
            << "  win-tiler ui-test-multi 0 0 1920 1080 1920 0 1920 1080\n"
            << "  win-tiler init-config config.toml\n"
            << "  win-tiler --config config.toml loop\n";
}

}  // namespace wintiler
