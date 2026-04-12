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
#include "policy.h"
#include "task.h"
#include "invariants.h"
#include <chrono>
#include <cmath>
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

    NullPolicy default_policy;
    Policy* policy = &default_policy;

    std::map<EntityId, Task> active_tasks;
    constexpr float kTaskArrivalRadius = 5.0f;
    constexpr float kTaskConfidenceThreshold = 0.5f;

    for (int tick = 0; tick < scn.ticks; ++tick) {
        // Movement — task override > policy override > default
        auto t0 = Clock::now();
        for (auto& e : entities) {
            auto task_it = active_tasks.find(e.id);
            if (task_it != active_tasks.end()) {
                Vec2 diff = task_it->second.target_position - e.position;
                float dist = diff.length();
                float step = e.speed * scn.dt;
                if (dist > 1e-9f && step < dist)
                    e.position = e.position + diff * (step / dist);
                else if (dist > 1e-9f)
                    e.position = task_it->second.target_position;
                replay.log(replay_entity_position(tick, e.id, e.position));
                continue;
            }
            if (e.can_sense) {
                PolicyObservation pobs;
                pobs.id = e.id;
                pobs.position = e.position;
                pobs.tick = tick;
                auto belief_it = beliefs.find(e.id);
                if (belief_it != beliefs.end()) {
                    // Copy top-K tracks by confidence into fixed-size observation
                    auto& tracks = belief_it->second.tracks;
                    // Simple selection: iterate and keep highest confidence
                    for (const auto& t : tracks) {
                        if (pobs.num_tracks < kMaxPolicyTracks) {
                            pobs.local_tracks[pobs.num_tracks++] = t;
                        } else {
                            // Replace lowest-confidence entry if this one is better
                            int worst = 0;
                            for (int j = 1; j < kMaxPolicyTracks; ++j)
                                if (pobs.local_tracks[j].confidence < pobs.local_tracks[worst].confidence)
                                    worst = j;
                            if (t.confidence > pobs.local_tracks[worst].confidence)
                                pobs.local_tracks[worst] = t;
                        }
                    }
                }
                {
                    auto target = policy->get_move_target(pobs);
                    if (target) {
                        Vec2 diff = *target - e.position;
                        float dist = diff.length();
                        float step = e.speed * scn.dt;
                        if (dist > 1e-9f && step < dist)
                            e.position = e.position + diff * (step / dist);
                        else if (dist > 1e-9f)
                            e.position = *target;
                        replay.log(replay_entity_position(tick, e.id, e.position));
                        continue;
                    }
                }
            }
            auto event = update_movement(e, scn.dt, rng);
            if (!e.waypoints.empty() || e.speed > 0.0f)
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
            std::vector<EntityId> detected_targets;
            for (auto* obs_ent : observables) {
                if (sensor->id == obs_ent->id) continue;  // skip self-sensing
                stats.sensors_updated++;
                stats.rays_cast++;

                Observation obs{};
                bool detected = sense(map, sensor->position, sensor->id,
                                      obs_ent->position, obs_ent->id,
                                      scn.max_sensor_range, tick, rng, obs,
                                      scn.perception.miss_rate);

                if (detected) {
                    stats.detections_generated++;
                    detected_targets.push_back(obs_ent->id);

                    // Direct self-observation for sensor-trackers
                    if (sensor->can_track)
                        beliefs[sensor->id].update(obs, tick);

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

            // Negative evidence: sensor looked but didn't see — reduce confidence
            if (sensor->can_track) {
                beliefs[sensor->id].apply_negative_evidence(
                    sensor->position, scn.max_sensor_range, map,
                    detected_targets, scn.belief.negative_evidence_factor);
            }

            // False positive generation
            if (scn.perception.false_positive_rate > 0.0f &&
                rng.uniform() < scn.perception.false_positive_rate) {
                float angle = rng.uniform() * 6.28318530f;
                float range = rng.uniform() * scn.max_sensor_range;
                Observation phantom{};
                phantom.tick = tick;
                phantom.observer = sensor->id;
                phantom.target = 0xFFFFFFFF;
                phantom.estimated_position = sensor->position +
                    Vec2{std::cos(angle), std::sin(angle)} * range;
                phantom.uncertainty = 2.0f;
                phantom.confidence = 0.2f + rng.uniform() * 0.3f;
                phantom.is_false_positive = true;

                if (sensor->can_track)
                    beliefs[sensor->id].update(phantom, tick);

                MessagePayload payload;
                payload.type = MessagePayload::OBSERVATION;
                payload.observation = phantom;

                auto t_replay = Clock::now();
                replay.log(replay_detection(tick, phantom));
                double r = elapsed_us(t_replay);
                sensing_replay_us += r;
                stats.replay_us += r;

                for (auto* tracker : trackers) {
                    float dist = (tracker->position - sensor->position).length();
                    int delivery_tick = comms.send(sensor->id, tracker->id, payload, tick,
                                                  dist, scn.channel, rng);
                    bool sent = delivery_tick >= 0;
                    if (sent) stats.messages_sent++;
                    else stats.messages_dropped++;

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
                    std::printf("tick %3d  %s[%u] FALSE POSITIVE at (%.1f,%.1f)\n",
                                tick, sensor->role_name.c_str(), sensor->id,
                                phantom.estimated_position.x, phantom.estimated_position.y);
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

        // Task completion
        std::vector<EntityId> completed_tasks;
        for (const auto& [eid, task] : active_tasks) {
            ScenarioEntity* assigned = nullptr;
            for (auto& e : entities) {
                if (e.id == eid) { assigned = &e; break; }
            }
            if (!assigned) continue;

            Vec2 diff = task.target_position - assigned->position;
            float dist_sq = diff.x * diff.x + diff.y * diff.y;
            if (dist_sq > kTaskArrivalRadius * kTaskArrivalRadius) continue;

            bool corroborated = false;
            for (auto& [gid, belief] : beliefs) {
                const Track* trk = belief.find_track(task.target_id);
                if (trk && trk->status == TrackStatus::FRESH) {
                    corroborated = true;
                    break;
                }
            }
            completed_tasks.push_back(eid);
            replay.log(replay_task_completed(tick, eid, task.target_id, corroborated));
            if (!bench_mode)
                std::printf("tick %3d  TASK COMPLETED: %s %u %s target %u\n",
                            tick, assigned->role_name.c_str(), eid,
                            corroborated ? "CORROBORATED" : "NOT FOUND", task.target_id);
        }
        for (EntityId eid : completed_tasks)
            active_tasks.erase(eid);

        // Task assignment — assign VERIFY for stale, low-confidence tracks
        for (auto* tracker_ent : trackers) {
            auto& belief = beliefs[tracker_ent->id];
            for (const auto& trk : belief.tracks) {
                if (trk.status != TrackStatus::STALE) continue;
                if (trk.confidence >= kTaskConfidenceThreshold) continue;

                bool already_tasked = false;
                for (const auto& [eid, t] : active_tasks) {
                    if (t.target_id == trk.target) { already_tasked = true; break; }
                }
                if (already_tasked) continue;

                ScenarioEntity* best = nullptr;
                float best_dist = 1e9f;
                for (auto& e : entities) {
                    if (!e.can_sense) continue;
                    if (active_tasks.count(e.id)) continue;
                    float d = (e.position - trk.estimated_position).length();
                    if (d < best_dist) {
                        best = &e;
                        best_dist = d;
                    }
                }

                if (best) {
                    Task task;
                    task.type = Task::Type::VERIFY;
                    task.assigned_to = best->id;
                    task.target_id = trk.target;
                    task.target_position = trk.estimated_position;
                    task.assigned_tick = tick;
                    active_tasks[best->id] = task;
                    replay.log(replay_task_assigned(tick, task));
                    if (!bench_mode)
                        std::printf("tick %3d  TASK ASSIGNED: %s %u -> VERIFY target %u at (%.1f,%.1f)\n",
                                    tick, best->role_name.c_str(), best->id,
                                    trk.target, trk.estimated_position.x, trk.estimated_position.y);
                }
            }
        }

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
