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

namespace {

struct CliHooks : RoundHooks {
    ReplayWriter* replay = nullptr;
    bool quiet = false;

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

    void on_unit_moved(int round, EntityId actor, GridPos from, GridPos to,
                       int ap_after) override {
        if (replay) replay->log(replay_unit_moved(round, actor, from, to, ap_after));
    }

    void on_shot_resolved(int round, EntityId shooter, EntityId target,
                          const ShotModifiers& m) override {
        if (replay) replay->log(replay_shot_resolved(round, shooter, target, m));
    }

    void on_damage(int round, EntityId shooter, EntityId target,
                   int damage, int hp_after, bool eliminated) override {
        if (replay)
            replay->log(replay_damage(round, shooter, target, damage, hp_after, eliminated));
    }

    void on_overwatch_set(int round, EntityId actor) override {
        if (replay) replay->log(replay_overwatch_set(round, actor));
    }

    void on_door_state_changed(int round, GridPos a, GridPos b,
                               DoorState new_state, const char* cause) override {
        if (replay) replay->log(replay_door_state(round, a, b, new_state, cause));
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
};

}  // namespace

int main(int argc, char* argv[]) {
    bool quiet = false;
    const char* path = nullptr;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--quiet") == 0 ||
            std::strcmp(argv[i], "--bench") == 0)
            quiet = true;
        else
            path = argv[i];
    }
    if (!path) {
        std::fprintf(stderr, "usage: xushi [--quiet] <scenario.json>\n");
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
    return 0;
}
