#include "game_mode.h"
#include "replay.h"
#include "replay_events.h"
#include "scenario.h"
#include "sim_engine.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <chrono>

namespace {

struct BenchMetrics {
    double total_us = 0.0;
    double activation_us = 0.0;
    int ticks = 0;
};

struct CliHooks : RoundHooks {
    ReplayWriter* replay = nullptr;
    bool quiet = false;
    bool bench = false;
    BenchMetrics* metrics = nullptr;
    std::chrono::steady_clock::time_point bench_start;

    void on_round_started(int round, int initiative_team) override {
        if (replay) replay->log(replay_round_started(round, initiative_team));
        if (!quiet)
            std::printf("round %2d  initiative: team %d\n", round, initiative_team);
    }

    void on_round_ended(int round) override {
        if (replay) replay->log(replay_round_ended(round));
    }

    void on_track_update(int round, int team, const Track& trk) override {
        if (replay) replay->log(replay_track_update(round, team, trk));
    }

    void on_track_expired(int round, int team, EntityId target) override {
        if (replay) replay->log(replay_track_expired(round, team, target));
    }

    void on_world_hash(int round, uint64_t hash) override {
        if (replay) replay->log(replay_world_hash(round, hash));
    }

    void on_game_mode_end(int round, const GameModeResult& r) override {
        if (replay) replay->log(replay_game_mode_end(round, r));
        if (!quiet) {
            if (r.winning_team >= 0)
                std::printf("round %2d  GAME OVER: team %d wins (%s)\n",
                            round, r.winning_team, r.reason.c_str());
            else
                std::printf("round %2d  GAME OVER: draw (%s)\n",
                            round, r.reason.c_str());
        }
    }

    void on_phase_timing(const char* phase, double elapsed_us) override {
        if (metrics) {
            if (std::strcmp(phase, "activation") == 0) {
                metrics->activation_us += elapsed_us;
                metrics->ticks++;
            }
            metrics->total_us += elapsed_us;
        }
    }
};

}  // namespace

int main(int argc, char* argv[]) {
    bool quiet = false;
    bool bench = false;
    const char* path = nullptr;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--quiet") == 0)
            quiet = true;
        else if (std::strcmp(argv[i], "--bench") == 0)
            bench = true;
        else
            path = argv[i];
    }
    if (bench) quiet = true;
    
    if (!path) {
        std::fprintf(stderr, "usage: xushi [--quiet] [--bench] <scenario.json>\n");
        return 1;
    }

    Scenario scn;
    try {
        scn = load_scenario(path);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }

    std::string replay_path = path;
    auto dot = replay_path.rfind('.');
    if (dot != std::string::npos) replay_path.resize(dot);
    replay_path += ".replay";

    std::unique_ptr<GameMode> game_mode;
    try {
        game_mode = create_game_mode(scn);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }

    SimEngine engine;
    engine.init(scn, game_mode.get());

    ReplayWriter replay(replay_path);
    replay.log(replay_header(scn, path));

    CliHooks hooks;
    hooks.replay = &replay;
    hooks.quiet = quiet;
    hooks.bench = bench;
    BenchMetrics metrics;
    if (bench) {
        hooks.metrics = &metrics;
        hooks.bench_start = std::chrono::steady_clock::now();
    }

    if (!quiet)
        std::printf("scenario: %s  seed: %llu  rounds: %d  replay: %s\n",
                    path,
                    static_cast<unsigned long long>(scn.seed),
                    scn.rounds,
                    replay_path.c_str());

    for (int round = 0; round < scn.rounds; ++round) {
        engine.run_round(round, hooks);
        if (engine.has_game_mode() && engine.game_mode_result().finished)
            break;
    }

    replay.close();
    
    // Print benchmark metrics
    if (bench) {
        auto bench_end = std::chrono::steady_clock::now();
        double total_ms = std::chrono::duration<double, std::milli>(bench_end - hooks.bench_start).count();
        int total_ticks = metrics.ticks;
        double throughput = total_ticks > 0 ? (total_ticks * 1000.0 / total_ms) : 0.0;
        double per_tick_ms = total_ticks > 0 ? (metrics.activation_us / 1000.0 / total_ticks) : 0.0;
        
        std::printf("throughput: %.1f ticks/sec\n", throughput);
        std::printf("per tick: %.4f ms\n", per_tick_ms);
        std::printf("sensing: 0.0 ms\n");  // Not currently measured
    }
    
    return 0;
}
