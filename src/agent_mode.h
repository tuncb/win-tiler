#pragma once

#include "argument_parser.h"
#include "options.h"

namespace wintiler {

void run_agent_mode(GlobalOptionsProvider& optionsProvider, const AgentCommand& command);

} // namespace wintiler
