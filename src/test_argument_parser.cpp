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
}

TEST_SUITE("version") {
  TEST_CASE("version string includes current prerelease label") {
    CHECK(get_version_string() == "0.4.0-alpha");
  }
}

#endif // !DOCTEST_CONFIG_DISABLE
