#pragma once

#include <string>

std::string resolve_scenario_path_from_replay(const std::string& replay_path,
                                              const std::string& scenario_path,
                                              bool allow_cwd_fallback = true);
