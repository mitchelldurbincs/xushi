#include "test_helpers.h"
#include "../src/path_resolve.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

static fs::path make_tmp_root() {
    const fs::path root = fs::temp_directory_path() / "xushi_replay_path_resolution_test";
    fs::remove_all(root);
    fs::create_directories(root);
    return root;
}

static void write_file(const fs::path& path, const std::string& content) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path);
    out << content;
}

static void test_absolute_path_kept(TestContext& ctx) {
    const fs::path root = make_tmp_root();
    const fs::path absolute = root / "scenarios" / "alpha.json";
    const std::string resolved = resolve_scenario_path_from_replay("ignored.replay", absolute.string());
    ctx.check(resolved == absolute.lexically_normal().string(), "absolute paths remain absolute");
}

static void test_relative_prefers_replay_parent(TestContext& ctx) {
    const fs::path root = make_tmp_root();
    const fs::path replay_dir = root / "runs" / "session";
    const fs::path cwd_dir = root / "cwd";
    const fs::path replay_path = replay_dir / "capture.replay";

    write_file(replay_dir / "scenarios" / "scene.json", "{}\n");
    write_file(cwd_dir / "scenarios" / "scene.json", "{\"wrong\":true}\n");
    write_file(replay_path, "\n");

    const fs::path original_cwd = fs::current_path();
    fs::current_path(cwd_dir);

    const std::string resolved = resolve_scenario_path_from_replay(replay_path.string(), "scenarios/scene.json");
    ctx.check(resolved == (replay_dir / "scenarios" / "scene.json").lexically_normal().string(),
          "relative path resolves against replay parent first");

    fs::current_path(original_cwd);
}

static void test_relative_falls_back_to_cwd(TestContext& ctx) {
    const fs::path root = make_tmp_root();
    const fs::path replay_dir = root / "runs" / "session";
    const fs::path cwd_dir = root / "cwd";
    const fs::path replay_path = replay_dir / "capture.replay";

    write_file(cwd_dir / "scenarios" / "fallback.json", "{}\n");
    write_file(replay_path, "\n");

    const fs::path original_cwd = fs::current_path();
    fs::current_path(cwd_dir);

    const std::string resolved = resolve_scenario_path_from_replay(replay_path.string(), "scenarios/fallback.json");
    ctx.check(resolved == fs::path("scenarios/fallback.json").lexically_normal().string(),
          "fallback keeps cwd-relative path for compatibility");

    fs::current_path(original_cwd);
}

int main() {
    return run_test_suite("replay path resolution", [](TestContext& ctx) {
    test_absolute_path_kept(ctx);
    test_relative_prefers_replay_parent(ctx);
    test_relative_falls_back_to_cwd(ctx);
    });
}
