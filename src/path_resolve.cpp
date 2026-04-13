#include "path_resolve.h"

#include <filesystem>

std::string resolve_scenario_path_from_replay(const std::string& replay_path,
                                              const std::string& scenario_path,
                                              bool allow_cwd_fallback) {
    namespace fs = std::filesystem;

    const fs::path scenario(scenario_path);
    if (scenario.is_absolute())
        return scenario.lexically_normal().string();

    const fs::path replay_parent = fs::path(replay_path).parent_path();
    const fs::path replay_relative_candidate = (replay_parent / scenario).lexically_normal();
    if (fs::exists(replay_relative_candidate))
        return replay_relative_candidate.string();

    if (allow_cwd_fallback)
        return scenario.lexically_normal().string();

    return replay_relative_candidate.string();
}
