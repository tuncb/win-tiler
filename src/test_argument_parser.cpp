#ifndef DOCTEST_CONFIG_DISABLE

#include <doctest/doctest.h>

#include <initializer_list>
#include <string>
#include <vector>

#include "argument_parser.h"
#include "version.h"

using namespace wintiler;

namespace {

ParseResult parse(std::initializer_list<const char*> rawArgs) {
  std::vector<std::string> storage;
  storage.reserve(rawArgs.size());
  for (const char* arg : rawArgs) {
    storage.emplace_back(arg);
  }

  std::vector<char*> argv;
  argv.reserve(storage.size());
  for (auto& arg : storage) {
    argv.push_back(arg.data());
  }

  return parse_args(static_cast<int>(argv.size()), argv.data());
}

} // namespace

TEST_SUITE("argument_parser") {
  TEST_CASE("defaults to loop when no command is provided") {
    auto result = parse({"win-tiler"});

    CHECK(result.success);
    REQUIRE(result.args.command.has_value());
    CHECK(std::holds_alternative<LoopCommand>(*result.args.command));
  }

  TEST_CASE("defaults to loop when only options are provided") {
    auto result = parse({"win-tiler", "--logmode", "debug"});

    CHECK(result.success);
    CHECK(result.args.options.log_level == LogLevel::Debug);
    REQUIRE(result.args.command.has_value());
    CHECK(std::holds_alternative<LoopCommand>(*result.args.command));
  }

  TEST_CASE("parses perf stats option with implicit loop command") {
    auto result = parse({"win-tiler", "--perf-stats"});

    CHECK(result.success);
    CHECK(result.args.options.perf_stats);
    REQUIRE(result.args.command.has_value());
    CHECK(std::holds_alternative<LoopCommand>(*result.args.command));
  }

  TEST_CASE("parses perf stats option with explicit loop command") {
    auto result = parse({"win-tiler", "--perf-stats", "loop"});

    CHECK(result.success);
    CHECK(result.args.options.perf_stats);
    REQUIRE(result.args.command.has_value());
    CHECK(std::holds_alternative<LoopCommand>(*result.args.command));
  }

  TEST_CASE("help still overrides the default loop command") {
    auto result = parse({"win-tiler", "--help"});

    CHECK(result.success);
    REQUIRE(result.args.command.has_value());
    CHECK(std::holds_alternative<HelpCommand>(*result.args.command));
  }

  TEST_CASE("parses startup enable command") {
    auto result = parse({"win-tiler", "startup", "enable"});

    CHECK(result.success);
    REQUIRE(result.args.command.has_value());
    REQUIRE(std::holds_alternative<StartupCommand>(*result.args.command));
    CHECK(std::get<StartupCommand>(*result.args.command).action == StartupAction::Enable);
  }

  TEST_CASE("parses startup disable command") {
    auto result = parse({"win-tiler", "startup", "disable"});

    CHECK(result.success);
    REQUIRE(result.args.command.has_value());
    REQUIRE(std::holds_alternative<StartupCommand>(*result.args.command));
    CHECK(std::get<StartupCommand>(*result.args.command).action == StartupAction::Disable);
  }

  TEST_CASE("parses startup status command") {
    auto result = parse({"win-tiler", "startup", "status"});

    CHECK(result.success);
    REQUIRE(result.args.command.has_value());
    REQUIRE(std::holds_alternative<StartupCommand>(*result.args.command));
    CHECK(std::get<StartupCommand>(*result.args.command).action == StartupAction::Status);
  }

  TEST_CASE("parses agent command with default stdio transport") {
    auto result = parse({"win-tiler", "agent"});

    CHECK(result.success);
    REQUIRE(result.args.command.has_value());
    REQUIRE(std::holds_alternative<AgentCommand>(*result.args.command));
    CHECK(std::get<AgentCommand>(*result.args.command).transport == AgentTransport::Stdio);
  }

  TEST_CASE("parses agent command with explicit stdio transport") {
    auto result = parse({"win-tiler", "agent", "stdio"});

    CHECK(result.success);
    REQUIRE(result.args.command.has_value());
    REQUIRE(std::holds_alternative<AgentCommand>(*result.args.command));
    CHECK(std::get<AgentCommand>(*result.args.command).transport == AgentTransport::Stdio);
  }

  TEST_CASE("agent command rejects unknown transport") {
    auto result = parse({"win-tiler", "agent", "pipe"});

    CHECK_FALSE(result.success);
    CHECK(result.error == "Unknown agent transport: pipe. Valid transports: stdio");
  }

  TEST_CASE("agent command rejects extra arguments") {
    auto result = parse({"win-tiler", "agent", "stdio", "extra"});

    CHECK_FALSE(result.success);
    CHECK(result.error == "agent does not accept extra arguments");
  }

  TEST_CASE("startup command requires an action") {
    auto result = parse({"win-tiler", "startup"});

    CHECK_FALSE(result.success);
    CHECK(result.error == "startup requires an action: enable, disable, or status");
  }

  TEST_CASE("startup command rejects extra arguments") {
    auto result = parse({"win-tiler", "startup", "enable", "extra"});

    CHECK_FALSE(result.success);
    CHECK(result.error == "startup does not accept extra arguments");
  }

  TEST_CASE("ui-test-monitor command is no longer supported") {
    auto result = parse({"win-tiler", "ui-test-monitor"});

    CHECK_FALSE(result.success);
    CHECK(result.error == "Unknown command: ui-test-monitor");
  }

  TEST_CASE("ui-test-multi command is no longer supported") {
    auto result = parse({"win-tiler", "ui-test-multi", "0", "0", "1920", "1080"});

    CHECK_FALSE(result.success);
    CHECK(result.error == "Unknown command: ui-test-multi");
  }
}

TEST_SUITE("version") {
  TEST_CASE("version string matches the current release version") {
    CHECK(get_version_string() == "0.5.1");
  }

  TEST_CASE("version string does not include a prerelease suffix") {
    CHECK(get_version_string().find('-') == std::string::npos);
  }
}

#endif // !DOCTEST_CONFIG_DISABLE
