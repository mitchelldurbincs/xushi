#include "sim_engine.h"
#include "sim.h"
#include "patrol_policy.h"
#include "game_mode.h"
#include "replay.h"
#include "replay_events.h"
#include "invariants.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

using Clock = std::chrono::high_resolution_clock;

static double elapsed_us(Clock::time_point start) {
    return std::chrono::duration<double, std::micro>(Clock::now() - start).count();
}

struct CLIHooks : TickHooks {
    ReplayWriter* replay;
    SystemStats* stats;
    bool bench_mode;

    const std::vector<ScenarioEntity>* entities;
    const std::map<EntityId, BeliefState>* beliefs;

    double sensing_replay_us = 0;

    const char* entity_role(EntityId id) const {
        for (const auto& e : *entities) {
            if (e.id == id) return e.role_name.c_str();
        }
        return "?";
    }

    const ScenarioEntity* find_entity(EntityId id) const {
        for (const auto& e : *entities) {
            if (e.id == id) return &e;
        }
        return nullptr;
    }

    void on_entity_moved(int tick, EntityId id, Vec2 pos) override {
        auto t = Clock::now();
        replay->log(replay_entity_position(tick, id, pos));
        double r = elapsed_us(t);
        stats->replay_us += r;
    }

    void on_waypoint_arrival(int tick, EntityId id, int waypoint_index, Vec2 pos) override {
        auto t = Clock::now();
        replay->log(replay_waypoint_arrival(tick, id, waypoint_index, pos));
        double r = elapsed_us(t);
        stats->replay_us += r;
    }

    void on_positions_check(const std::vector<ScenarioEntity>& ents) override {
        check_positions_finite(ents, "after movement");
    }

    void on_detection(int tick, const Observation& obs) override {
        auto t = Clock::now();
        replay->log(replay_detection(tick, obs));
        double r = elapsed_us(t);
        sensing_replay_us += r;
        stats->replay_us += r;

        if (!bench_mode && !obs.is_false_positive) {
            auto* sensor = find_entity(obs.observer);
            const char* sensor_name = sensor ? sensor->role_name.c_str() : "?";
            auto* target = find_entity(obs.target);
            const char* target_name = target ? target->role_name.c_str() : "?";
            std::printf("tick %3d  %s[%u] detected %s %u\n",
                        tick, sensor_name, obs.observer, target_name, obs.target);
        }
    }

    void on_miss(int tick, EntityId sensor) override {
        if (!bench_mode) {
            std::printf("tick %3d  %s[%u] ---\n",
                        tick, entity_role(sensor), sensor);
        }
    }

    void on_false_positive(int tick, EntityId sensor, const Observation& phantom) override {
        if (!bench_mode)
            std::printf("tick %3d  %s[%u] FALSE POSITIVE at (%.1f,%.1f)\n",
                        tick, entity_role(sensor), sensor,
                        phantom.estimated_position.x, phantom.estimated_position.y);
    }

    void on_msg_sent(int tick, EntityId sender, EntityId receiver, int delivery_tick) override {
        auto t = Clock::now();
        replay->log(replay_msg_sent(tick, sender, receiver, delivery_tick));
        double r = elapsed_us(t);
        sensing_replay_us += r;
        stats->replay_us += r;
    }

    void on_msg_dropped(int tick, EntityId sender, EntityId receiver) override {
        auto t = Clock::now();
        replay->log(replay_msg_dropped(tick, sender, receiver));
        double r = elapsed_us(t);
        sensing_replay_us += r;
        stats->replay_us += r;
    }

    void on_msg_delivered(int tick, EntityId sender, EntityId receiver) override {
        replay->log(replay_msg_delivered(tick, sender, receiver));
    }

    void on_track_update(int tick, EntityId owner, const Track& trk) override {
        replay->log(replay_track_update(tick, owner, trk));
    }

    void on_track_expired(int tick, EntityId owner, EntityId target) override {
        replay->log(replay_track_expired(tick, owner, target));
    }

    void on_action_resolved(int tick, const ActionResult& result) override {
        replay->log(replay_action_resolved(tick, result));
        if (!bench_mode)
            std::printf("tick %3d  ACTION: %s actor=%u target=%u %s\n",
                        tick, action_type_str(result.request.type),
                        result.request.actor, result.request.track_target,
                        result.allowed ? "ACCEPTED" : "REJECTED");
    }

    void on_effect_resolved(int tick, const EffectOutcome& outcome) override {
        replay->log(replay_effect_resolved(tick, outcome));
        if (!bench_mode) {
            std::printf("tick %3d  EFFECT: actor=%u target=%u %s vitality %d->%d ammo %d->%d cd %d->%d\n",
                        tick, outcome.request.actor, outcome.request.track_target,
                        outcome.hit ? "HIT" : "MISS",
                        outcome.vitality_before, outcome.vitality_after,
                        outcome.actor_ammo_before, outcome.actor_ammo_after,
                        outcome.actor_cooldown_before, outcome.actor_cooldown_after);
        }
    }

    void on_belief_invariant_check(const BeliefState& belief) override {
        check_belief_invariants(belief, "after belief decay");
    }

    void on_task_assigned(int tick, const Task& task, const ScenarioEntity& entity) override {
        replay->log(replay_task_assigned(tick, task));
        if (!bench_mode)
            std::printf("tick %3d  TASK ASSIGNED: %s %u -> VERIFY target %u at (%.1f,%.1f)\n",
                        tick, entity.role_name.c_str(), entity.id,
                        task.target_id, task.target_position.x, task.target_position.y);
    }

    void on_task_completed(int tick, EntityId entity, EntityId target, bool corroborated) override {
        replay->log(replay_task_completed(tick, entity, target, corroborated));
        if (!bench_mode) {
            std::printf("tick %3d  TASK COMPLETED: %s[%u] %s target %u\n",
                        tick, entity_role(entity), entity,
                        corroborated ? "CORROBORATED" : "NOT FOUND", target);
        }
    }

    void on_game_mode_end(int tick, const GameModeResult& result) override {
        if (!bench_mode) {
            if (result.winning_team >= 0)
                std::printf("tick %3d  GAME OVER: team %d wins (%s)\n",
                            tick, result.winning_team, result.reason.c_str());
            else
                std::printf("tick %3d  GAME OVER: draw (%s)\n",
                            tick, result.reason.c_str());
        }
    }

    void on_world_hash(int tick, uint64_t hash) override {
        replay->log(replay_world_hash(tick, hash));
    }

    void on_stats_snapshot(int tick, const SystemStats& s) override {
        replay->log(replay_stats(tick, s));
    }
};

int main(int argc, char* argv[]) {
    bool bench_mode = false;
    const char* path = "scenarios/default.json";

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--bench") == 0)
            bench_mode = true;
        else
            path = argv[i];
    }

    Scenario scn;
    try {
        scn = load_scenario(path);
    } catch (const std::runtime_error& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }

    bool has_sensors = false, has_trackers = false, has_observables = false;
    for (const auto& e : scn.entities) {
        if (e.can_sense)     has_sensors = true;
        if (e.can_track)     has_trackers = true;
        if (e.is_observable) has_observables = true;
    }
    if (!has_sensors || !has_trackers || !has_observables) {
        std::fprintf(stderr, "error: scenario missing required capabilities\n");
        return 1;
    }

    std::string replay_path = path;
    auto dot = replay_path.rfind('.');
    if (dot != std::string::npos)
        replay_path.resize(dot);
    replay_path += ".replay";

    // Construct policy from scenario config
    std::unique_ptr<Policy> policy;
    if (scn.policy_config.type == "patrol") {
        auto pp = std::make_unique<PatrolPolicy>();
        for (const auto& [eid, wps] : scn.policy_config.patrol_routes)
            pp->routes[eid] = {wps, 0};
        policy = std::move(pp);
    }

    // Construct game mode from scenario config (nullptr if none configured)
    std::unique_ptr<GameMode> game_mode;
    try {
        game_mode = create_game_mode(scn);
    } catch (const std::runtime_error& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }

    SimEngine engine;
    engine.init(scn, policy.get(), game_mode.get());

    ReplayWriter replay(replay_path);
    replay.log(replay_header(scn, path));

    if (!bench_mode) {
        std::printf("scenario: %s  seed: %llu  ticks: %d  replay: %s\n",
                    path, static_cast<unsigned long long>(scn.seed), scn.ticks, replay_path.c_str());
        int ns = 0, nt = 0, no = 0;
        for (const auto& e : scn.entities) {
            if (e.can_sense) ns++;
            if (e.can_track) nt++;
            if (e.is_observable) no++;
        }
        std::printf("  sensors: %d  trackers: %d  observables: %d\n\n", ns, nt, no);
    }

    CLIHooks hooks;
    hooks.replay = &replay;
    hooks.stats = &engine.stats();
    hooks.bench_mode = bench_mode;
    hooks.entities = &engine.get_entities();
    hooks.beliefs = &engine.get_beliefs();

    for (int tick = 0; tick < scn.ticks; ++tick) {
        hooks.sensing_replay_us = 0;

        // Auto-designate: for each tracker, designate fresh tracks without existing designations
        for (const auto& [eid, belief] : engine.get_beliefs()) {
            for (const auto& trk : belief.tracks) {
                if (trk.status != TrackStatus::FRESH) continue;
                bool already = false;
                for (const auto& d : engine.get_designations()) {
                    if (d.track_target == trk.target && d.issuer == eid) {
                        already = true; break;
                    }
                }
                if (already) continue;
                ActionRequest req;
                req.actor = eid;
                req.type = ActionType::DesignateTrack;
                req.track_target = trk.target;
                req.desig_kind = DesignationKind::Observe;
                req.priority = 1;
                engine.submit_action(req);
            }
        }

        // Auto-engage: actors with can_engage attempt to engage fresh enemy tracks
        for (const auto& e : engine.get_entities()) {
            if (!e.can_engage) continue;
            if (e.vitality <= 0) continue;
            if (e.cooldown_ticks_remaining > 0) continue;
            if (e.allowed_effect_profile_indices.empty()) continue;
            auto bit = engine.get_beliefs().find(e.id);
            if (bit == engine.get_beliefs().end()) continue;
            for (const auto& trk : bit->second.tracks) {
                if (trk.status != TrackStatus::FRESH) continue;
                // Find target entity to check team
                const ScenarioEntity* target_ent = nullptr;
                for (const auto& te : engine.get_entities()) {
                    if (te.id == trk.target) { target_ent = &te; break; }
                }
                // Skip same-team targets
                if (target_ent && e.team >= 0 && target_ent->team >= 0 &&
                    e.team == target_ent->team)
                    continue;
                ActionRequest req;
                req.actor = e.id;
                req.type = ActionType::EngageTrack;
                req.track_target = trk.target;
                req.effect_profile_index = static_cast<uint32_t>(e.allowed_effect_profile_indices[0]);
                engine.submit_action(req);
                break;  // one engagement per actor per tick
            }
        }

        auto t0 = Clock::now();
        engine.step(tick, hooks);
        double tick_us = elapsed_us(t0);
        (void)tick_us;

        // Check for game mode early termination
        if (engine.has_game_mode() && engine.game_mode_result().finished)
            break;

        if (!bench_mode) {
            for (const auto& e : engine.get_entities()) {
                if (!e.can_track) continue;
                auto bit = engine.get_beliefs().find(e.id);
                if (bit == engine.get_beliefs().end()) continue;
                const auto& belief = bit->second;
                for (const auto& obs_e : engine.get_entities()) {
                    if (!obs_e.is_observable) continue;
                    const Track* trk = belief.find_track(obs_e.id);
                    if (trk) {
                        int age = tick - trk->last_update_tick;
                        std::printf("         %s[%u] belief: %s %u at (%5.1f,%5.1f)  "
                                    "conf:%.2f  unc:%.1f  age:%d  [%s]\n",
                                    e.role_name.c_str(), e.id,
                                    obs_e.role_name.c_str(), obs_e.id,
                                    trk->estimated_position.x, trk->estimated_position.y,
                                    trk->confidence, trk->uncertainty, age,
                                    track_status_str(trk->status));
                    } else {
                        std::printf("         %s[%u] belief: %s %u no track\n",
                                    e.role_name.c_str(), e.id,
                                    obs_e.role_name.c_str(), obs_e.id);
                    }
                }
            }
        }
    }

    replay.close();
    engine.stats().print_summary(scn.ticks);

    return 0;
}
