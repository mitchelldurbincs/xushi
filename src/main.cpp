#include "types.h"
#include "map.h"
#include "sensing.h"
#include "comm.h"
#include "belief.h"
#include "rng.h"
#include "scenario.h"
#include "sim.h"
#include "replay.h"
#include "replay_events.h"
#include "stats.h"
#include "movement.h"
#include "invariants.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

using Clock = std::chrono::high_resolution_clock;

static double elapsed_us(Clock::time_point start) {
    return std::chrono::duration<double, std::micro>(Clock::now() - start).count();
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

    std::vector<ScenarioEntity*> sensors;
    std::vector<ScenarioEntity*> trackers;
    std::vector<ScenarioEntity*> observables;
    for (auto& e : entities) {
        if (e.can_sense)     sensors.push_back(&e);
        if (e.can_track)     trackers.push_back(&e);
        if (e.is_observable) observables.push_back(&e);
    }

    if (sensors.empty() || trackers.empty() || observables.empty()) {
        std::fprintf(stderr, "error: scenario missing required capabilities\n");
        return 1;
    }

    Rng rng(scn.seed);
    CommSystem comms;
    std::map<EntityId, BeliefState> beliefs;
    for (auto* t : trackers)
        beliefs[t->id] = BeliefState{};
    SystemStats stats;

    ReplayWriter replay(replay_path);
    replay.log(replay_header(scn, path));

    if (!bench_mode) {
        std::printf("scenario: %s  seed: %llu  ticks: %d  replay: %s\n",
                    path, static_cast<unsigned long long>(scn.seed), scn.ticks, replay_path.c_str());
        std::printf("  sensors: %zu  trackers: %zu  observables: %zu\n\n",
                    sensors.size(), trackers.size(), observables.size());
    }

    for (int tick = 0; tick < scn.ticks; ++tick) {
        // Movement
        auto t0 = Clock::now();
        for (auto& e : entities) {
            auto event = update_movement(e, scn.dt);
            if (!e.waypoints.empty())
                replay.log(replay_entity_position(tick, e.id, e.position));
            if (event.arrived)
                replay.log(replay_waypoint_arrival(tick, e.id, event.waypoint_index, e.position));
        }
        stats.movement_us += elapsed_us(t0);
        check_positions_finite(entities, "after movement");

        // Sensing — each sensor observes all observables, broadcasts to all trackers
        t0 = Clock::now();
        double sensing_replay_us = 0;
        for (auto* sensor : sensors) {
            for (auto* obs_ent : observables) {
                if (sensor->id == obs_ent->id) continue;  // skip self-sensing
                stats.sensors_updated++;
                stats.rays_cast++;

                Observation obs{};
                bool detected = sense(map, sensor->position, sensor->id,
                                      obs_ent->position, obs_ent->id,
                                      scn.max_sensor_range, tick, rng, obs);

                if (detected) {
                    stats.detections_generated++;

                    MessagePayload payload;
                    payload.type = MessagePayload::OBSERVATION;
                    payload.observation = obs;

                    // Log detection once per sensor-observable pair
                    auto t_replay = Clock::now();
                    replay.log(replay_detection(tick, obs));
                    double r = elapsed_us(t_replay);
                    sensing_replay_us += r;
                    stats.replay_us += r;

                    // Broadcast to all trackers
                    for (auto* tracker : trackers) {
                        float dist = (tracker->position - sensor->position).length();
                        int delivery_tick = comms.send(sensor->id, tracker->id, payload, tick,
                                                      dist, scn.channel, rng);
                        bool sent = delivery_tick >= 0;

                        if (sent) {
                            stats.messages_sent++;
                        } else {
                            stats.messages_dropped++;
                        }

                        t_replay = Clock::now();
                        if (sent)
                            replay.log(replay_msg_sent(tick, sensor->id, tracker->id, delivery_tick));
                        else
                            replay.log(replay_msg_dropped(tick, sensor->id, tracker->id));
                        r = elapsed_us(t_replay);
                        sensing_replay_us += r;
                        stats.replay_us += r;
                    }

                    if (!bench_mode)
                        std::printf("tick %3d  %s[%u] detected %s %u\n",
                                    tick, sensor->role_name.c_str(), sensor->id,
                                    obs_ent->role_name.c_str(), obs_ent->id);
                } else {
                    if (!bench_mode)
                        std::printf("tick %3d  %s[%u] ---\n",
                                    tick, sensor->role_name.c_str(), sensor->id);
                }
            }
        }
        stats.sensing_us += elapsed_us(t0) - sensing_replay_us;

        // Communication
        t0 = Clock::now();
        std::vector<Message> delivered;
        comms.deliver(tick, delivered);
        stats.comm_us += elapsed_us(t0);

        // Track expirations (snapshot before belief update)
        std::map<EntityId, std::vector<EntityId>> tracked_before;
        for (auto& [gid, belief] : beliefs) {
            for (const auto& t : belief.tracks)
                tracked_before[gid].push_back(t.target);
        }

        // Belief — route messages to correct tracker's belief
        t0 = Clock::now();
        for (const auto& msg : delivered) {
            stats.messages_delivered++;
            auto it = beliefs.find(msg.receiver);
            if (it != beliefs.end())
                it->second.update(msg.payload.observation, tick);
        }
        for (auto& [gid, belief] : beliefs)
            belief.decay(tick, scn.dt, scn.belief);
        stats.belief_us += elapsed_us(t0);

        for (auto& [gid, belief] : beliefs)
            check_belief_invariants(belief, "after belief decay");

        // Aggregate stats across all trackers
        int total_active = 0;
        for (auto& [gid, belief] : beliefs) {
            total_active += static_cast<int>(belief.tracks.size());
            for (EntityId id : tracked_before[gid]) {
                if (!belief.find_track(id))
                    stats.tracks_expired++;
            }
        }
        stats.tracks_active = total_active;

        // Replay logging for comms/belief
        t0 = Clock::now();
        for (const auto& msg : delivered)
            replay.log(replay_msg_delivered(tick, msg.sender, msg.receiver));
        for (auto& [gid, belief] : beliefs) {
            for (const auto& t : belief.tracks)
                replay.log(replay_track_update(tick, gid, t));
            for (EntityId id : tracked_before[gid]) {
                if (!belief.find_track(id))
                    replay.log(replay_track_expired(tick, gid, id));
            }
        }
        if (tick % 10 == 0) {
            replay.log(replay_world_hash(tick, compute_world_hash(entities, beliefs)));
            replay.log(replay_stats(tick, stats));
        }
        stats.replay_us += elapsed_us(t0);

        // Print belief
        if (!bench_mode) {
            for (auto* tracker : trackers) {
                auto& belief = beliefs[tracker->id];
                for (auto* obs_ent : observables) {
                    const Track* trk = belief.find_track(obs_ent->id);
                    if (trk) {
                        int age = tick - trk->last_update_tick;
                        std::printf("         %s[%u] belief: %s %u at (%5.1f,%5.1f)  "
                                    "conf:%.2f  unc:%.1f  age:%d  [%s]\n",
                                    tracker->role_name.c_str(), tracker->id,
                                    obs_ent->role_name.c_str(), obs_ent->id,
                                    trk->estimated_position.x, trk->estimated_position.y,
                                    trk->confidence, trk->uncertainty, age,
                                    track_status_str(trk->status));
                    } else {
                        std::printf("         %s[%u] belief: %s %u no track\n",
                                    tracker->role_name.c_str(), tracker->id,
                                    obs_ent->role_name.c_str(), obs_ent->id);
                    }
                }
            }
        }
    }

    replay.close();
    stats.print_summary(scn.ticks);

    return 0;
}
