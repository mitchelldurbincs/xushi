#include "types.h"
#include "map.h"
#include "sensing.h"
#include "comm.h"
#include "belief.h"
#include "rng.h"
#include "scenario.h"
#include "replay.h"
#include "replay_events.h"
#include "stats.h"
#include "invariants.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>

using Clock = std::chrono::high_resolution_clock;

static double elapsed_us(Clock::time_point start) {
    return std::chrono::duration<double, std::micro>(Clock::now() - start).count();
}

static uint64_t compute_world_hash(const std::vector<ScenarioEntity>& entities,
                                    const BeliefState& belief) {
    uint64_t h = 14695981039346656037ULL;
    auto mix = [&](const void* data, size_t len) {
        const auto* bytes = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < len; ++i) {
            h ^= bytes[i];
            h *= 1099511628211ULL;
        }
    };
    for (const auto& e : entities) {
        mix(&e.id, sizeof(e.id));
        mix(&e.position.x, sizeof(float));
        mix(&e.position.y, sizeof(float));
    }
    for (const auto& t : belief.tracks) {
        mix(&t.target, sizeof(t.target));
        mix(&t.estimated_position.x, sizeof(float));
        mix(&t.estimated_position.y, sizeof(float));
        mix(&t.confidence, sizeof(float));
        mix(&t.uncertainty, sizeof(float));
    }
    return h;
}

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

    std::string replay_path = path;
    auto dot = replay_path.rfind('.');
    if (dot != std::string::npos)
        replay_path = replay_path.substr(0, dot);
    replay_path += ".replay";

    Map map;
    map.obstacles = scn.obstacles;

    std::vector<ScenarioEntity> entities = scn.entities;

    ScenarioEntity* drone = nullptr;
    ScenarioEntity* ground = nullptr;
    std::vector<ScenarioEntity*> targets;
    for (auto& e : entities) {
        if (e.type == "drone")  drone = &e;
        if (e.type == "ground") ground = &e;
        if (e.type == "target") targets.push_back(&e);
    }

    if (!drone || !ground || targets.empty()) {
        std::fprintf(stderr, "error: scenario must have at least one drone, ground, and target\n");
        return 1;
    }

    Rng rng(scn.seed);
    CommSystem comms;
    BeliefState ground_belief;
    SystemStats stats;

    ReplayWriter replay(replay_path);
    replay.log(replay_header(scn, path));

    if (!bench_mode) {
        std::printf("scenario: %s  seed: %llu  ticks: %d  replay: %s\n\n",
                    path, static_cast<unsigned long long>(scn.seed), scn.ticks, replay_path.c_str());
    }

    for (int tick = 0; tick < scn.ticks; ++tick) {
        // Movement
        auto t0 = Clock::now();
        for (auto& e : entities)
            e.position = e.position + e.velocity * scn.dt;
        stats.movement_us += elapsed_us(t0);
        check_positions_finite(entities, "after movement");

        // Sensing
        t0 = Clock::now();
        double sensing_replay_us = 0;
        for (auto* tgt : targets) {
            stats.sensors_updated++;
            stats.rays_cast++;

            Observation obs{};
            bool detected = sense(map, drone->position, drone->id,
                                  tgt->position, tgt->id,
                                  scn.max_sensor_range, tick, rng, obs);

            if (detected) {
                stats.detections_generated++;

                float dist = (ground->position - drone->position).length();
                MessagePayload payload;
                payload.type = MessagePayload::OBSERVATION;
                payload.observation = obs;

                int delivery_tick = comms.send(drone->id, ground->id, payload, tick,
                                              dist, scn.channel, rng);
                bool sent = delivery_tick >= 0;

                if (sent) {
                    stats.messages_sent++;
                } else {
                    stats.messages_dropped++;
                }

                auto t_replay = Clock::now();
                replay.log(replay_detection(tick, obs));
                if (sent)
                    replay.log(replay_msg_sent(tick, drone->id, ground->id, delivery_tick));
                else
                    replay.log(replay_msg_dropped(tick, drone->id, ground->id));
                double r = elapsed_us(t_replay);
                sensing_replay_us += r;
                stats.replay_us += r;

                if (!bench_mode)
                    std::printf("tick %3d  DRONE detected target %u\n", tick, tgt->id);
            } else {
                if (!bench_mode)
                    std::printf("tick %3d  DRONE ---\n", tick);
            }
        }
        stats.sensing_us += elapsed_us(t0) - sensing_replay_us;

        // Communication
        t0 = Clock::now();
        std::vector<Message> delivered;
        comms.deliver(tick, delivered);
        stats.comm_us += elapsed_us(t0);

        // Track expirations (before belief update)
        std::vector<EntityId> tracked_before;
        for (const auto& t : ground_belief.tracks)
            tracked_before.push_back(t.target);

        // Belief
        t0 = Clock::now();
        for (const auto& msg : delivered) {
            stats.messages_delivered++;
            ground_belief.update(msg.payload.observation, tick);
        }
        ground_belief.decay(tick, scn.dt, scn.belief);
        stats.belief_us += elapsed_us(t0);
        check_belief_invariants(ground_belief, "after belief decay");

        stats.tracks_active = static_cast<int>(ground_belief.tracks.size());

        // Detect expirations
        for (EntityId id : tracked_before) {
            if (!ground_belief.find_track(id))
                stats.tracks_expired++;
        }

        // Replay logging for comms/belief
        t0 = Clock::now();
        for (const auto& msg : delivered)
            replay.log(replay_msg_delivered(tick, msg.sender, msg.receiver));
        for (const auto& t : ground_belief.tracks)
            replay.log(replay_track_update(tick, ground->id, t));
        for (EntityId id : tracked_before) {
            if (!ground_belief.find_track(id))
                replay.log(replay_track_expired(tick, ground->id, id));
        }
        if (tick % 10 == 0) {
            replay.log(replay_world_hash(tick, compute_world_hash(entities, ground_belief)));
            replay.log(replay_stats(tick, stats));
        }
        stats.replay_us += elapsed_us(t0);

        // Print belief
        if (!bench_mode) {
            for (auto* tgt : targets) {
                const Track* trk = ground_belief.find_track(tgt->id);
                if (trk) {
                    int age = tick - trk->last_update_tick;
                    std::printf("         GROUND belief: target %u at (%5.1f,%5.1f)  "
                                "conf:%.2f  unc:%.1f  age:%d  [%s]\n",
                                tgt->id,
                                trk->estimated_position.x, trk->estimated_position.y,
                                trk->confidence, trk->uncertainty, age,
                                track_status_str(trk->status));
                } else {
                    std::printf("         GROUND belief: target %u no track\n", tgt->id);
                }
            }
        }
    }

    replay.close();
    stats.print_summary(scn.ticks);

    return 0;
}
