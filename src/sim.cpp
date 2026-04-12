#include "sim.h"
#include "map.h"
#include "sensing.h"
#include "comm.h"
#include "belief.h"
#include "movement.h"
#include "policy.h"
#include "task.h"
#include "rng.h"
#include <cmath>

uint64_t compute_world_hash(const std::vector<ScenarioEntity>& entities,
                            const std::map<EntityId, BeliefState>& beliefs) {
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
        mix(&e.current_waypoint, sizeof(e.current_waypoint));
    }
    for (const auto& [owner_id, belief] : beliefs) {
        mix(&owner_id, sizeof(owner_id));
        for (const auto& t : belief.tracks) {
            mix(&t.target, sizeof(t.target));
            mix(&t.estimated_position.x, sizeof(float));
            mix(&t.estimated_position.y, sizeof(float));
            mix(&t.confidence, sizeof(float));
            mix(&t.uncertainty, sizeof(float));
        }
    }
    return h;
}

SimResult run_scenario_headless(const Scenario& scn) {
    SimResult result{};

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

    if (sensors.empty() || trackers.empty() || observables.empty())
        return result;

    Rng rng(scn.seed);
    CommSystem comms;
    std::map<EntityId, BeliefState> beliefs;
    for (auto* t : trackers)
        beliefs[t->id] = BeliefState{};

    NullPolicy default_policy;
    Policy* policy = &default_policy;

    std::map<EntityId, Task> active_tasks;
    constexpr float kTaskArrivalRadius = 5.0f;
    constexpr float kTaskConfidenceThreshold = 0.5f;

    for (int tick = 0; tick < scn.ticks; ++tick) {
        // Movement — task override > policy override > default
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
                continue;
            }
            if (e.can_sense) {
                auto it = beliefs.find(e.id);
                if (it != beliefs.end()) {
                    auto target = policy->get_move_target(e.id, it->second, tick);
                    if (target) {
                        Vec2 diff = *target - e.position;
                        float dist = diff.length();
                        float step = e.speed * scn.dt;
                        if (dist > 1e-9f && step < dist)
                            e.position = e.position + diff * (step / dist);
                        else if (dist > 1e-9f)
                            e.position = *target;
                        continue;
                    }
                }
            }
            update_movement(e, scn.dt, rng);
        }

        // Sensing
        for (auto* sensor : sensors) {
            std::vector<EntityId> detected_targets;
            for (auto* obs_ent : observables) {
                if (sensor->id == obs_ent->id) continue;
                result.stats.sensors_updated++;
                result.stats.rays_cast++;

                Observation obs{};
                bool detected = sense(map, sensor->position, sensor->id,
                                      obs_ent->position, obs_ent->id,
                                      scn.max_sensor_range, tick, rng, obs,
                                      scn.perception.miss_rate);

                if (detected) {
                    result.stats.detections_generated++;
                    detected_targets.push_back(obs_ent->id);

                    if (sensor->can_track)
                        beliefs[sensor->id].update(obs, tick);

                    MessagePayload payload;
                    payload.type = MessagePayload::OBSERVATION;
                    payload.observation = obs;

                    for (auto* tracker : trackers) {
                        float dist = (tracker->position - sensor->position).length();
                        int dt = comms.send(sensor->id, tracker->id, payload, tick,
                                            dist, scn.channel, rng);
                        if (dt >= 0)
                            result.stats.messages_sent++;
                        else
                            result.stats.messages_dropped++;
                    }
                }
            }

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
                for (auto* tracker : trackers) {
                    float dist = (tracker->position - sensor->position).length();
                    int dt = comms.send(sensor->id, tracker->id, payload, tick,
                                        dist, scn.channel, rng);
                    if (dt >= 0)
                        result.stats.messages_sent++;
                    else
                        result.stats.messages_dropped++;
                }
            }
        }

        // Communication
        std::vector<Message> delivered;
        comms.deliver(tick, delivered);

        // Track expirations (snapshot before update)
        std::map<EntityId, std::vector<EntityId>> tracked_before;
        for (auto& [gid, belief] : beliefs) {
            for (const auto& t : belief.tracks)
                tracked_before[gid].push_back(t.target);
        }

        // Belief
        for (const auto& msg : delivered) {
            result.stats.messages_delivered++;
            auto it = beliefs.find(msg.receiver);
            if (it != beliefs.end())
                it->second.update(msg.payload.observation, tick);
        }
        for (auto& [gid, belief] : beliefs)
            belief.decay(tick, scn.dt, scn.belief);

        // Aggregate stats
        int total_active = 0;
        for (auto& [gid, belief] : beliefs) {
            total_active += static_cast<int>(belief.tracks.size());
            for (EntityId id : tracked_before[gid]) {
                if (!belief.find_track(id))
                    result.stats.tracks_expired++;
            }
        }
        result.stats.tracks_active = total_active;

        // Task completion
        std::vector<EntityId> completed_tasks;
        for (const auto& [eid, task] : active_tasks) {
            // Find entity
            ScenarioEntity* assigned = nullptr;
            for (auto& e : entities) {
                if (e.id == eid) { assigned = &e; break; }
            }
            if (!assigned) continue;

            Vec2 diff = task.target_position - assigned->position;
            float dist_sq = diff.x * diff.x + diff.y * diff.y;
            if (dist_sq > kTaskArrivalRadius * kTaskArrivalRadius) continue;

            // Arrived — check if target was corroborated
            bool corroborated = false;
            for (auto& [gid, belief] : beliefs) {
                const Track* trk = belief.find_track(task.target_id);
                if (trk && trk->status == TrackStatus::FRESH) {
                    corroborated = true;
                    break;
                }
            }
            completed_tasks.push_back(eid);
            result.tasks_completed++;
        }
        for (EntityId eid : completed_tasks)
            active_tasks.erase(eid);

        // Task assignment — assign VERIFY for stale tracks
        for (auto* tracker_ent : trackers) {
            auto& belief = beliefs[tracker_ent->id];
            for (const auto& trk : belief.tracks) {
                if (trk.status != TrackStatus::STALE) continue;
                if (trk.confidence >= kTaskConfidenceThreshold) continue;

                // Check if any entity already tasked for this target
                bool already_tasked = false;
                for (const auto& [eid, t] : active_tasks) {
                    if (t.target_id == trk.target) { already_tasked = true; break; }
                }
                if (already_tasked) continue;

                // Find nearest idle entity with can_sense
                ScenarioEntity* best = nullptr;
                float best_dist = 1e9f;
                for (auto& e : entities) {
                    if (!e.can_sense) continue;
                    if (e.is_observable && !e.can_track && !e.can_sense) continue;
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
                    result.tasks_assigned++;
                }
            }
        }

        // World hash every 10 ticks
        if (tick % 10 == 0)
            result.world_hashes.push_back(compute_world_hash(entities, beliefs));
    }

    result.final_track_count = 0;
    for (const auto& [gid, belief] : beliefs)
        result.final_track_count += static_cast<int>(belief.tracks.size());
    return result;
}
