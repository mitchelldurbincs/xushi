#include "sim.h"
#include "map.h"
#include "sensing.h"
#include "comm.h"
#include "belief.h"
#include "movement.h"
#include "policy.h"
#include "rng.h"

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

    for (int tick = 0; tick < scn.ticks; ++tick) {
        // Movement (policy can override for sensor entities)
        for (auto& e : entities) {
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

        // Sensing — each sensor observes all observables, broadcasts to all trackers
        for (auto* sensor : sensors) {
            std::vector<EntityId> detected_targets;
            for (auto* obs_ent : observables) {
                if (sensor->id == obs_ent->id) continue;  // skip self-sensing
                result.stats.sensors_updated++;
                result.stats.rays_cast++;

                Observation obs{};
                bool detected = sense(map, sensor->position, sensor->id,
                                      obs_ent->position, obs_ent->id,
                                      scn.max_sensor_range, tick, rng, obs);

                if (detected) {
                    result.stats.detections_generated++;
                    detected_targets.push_back(obs_ent->id);

                    // Direct self-observation for sensor-trackers
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

            // Negative evidence: sensor looked but didn't see — reduce confidence
            if (sensor->can_track) {
                beliefs[sensor->id].apply_negative_evidence(
                    sensor->position, scn.max_sensor_range, map,
                    detected_targets, scn.belief.negative_evidence_factor);
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

        // Belief — route messages to correct ground's belief
        for (const auto& msg : delivered) {
            result.stats.messages_delivered++;
            auto it = beliefs.find(msg.receiver);
            if (it != beliefs.end())
                it->second.update(msg.payload.observation, tick);
        }
        for (auto& [gid, belief] : beliefs)
            belief.decay(tick, scn.dt, scn.belief);

        // Aggregate stats across all grounds
        int total_active = 0;
        for (auto& [gid, belief] : beliefs) {
            total_active += static_cast<int>(belief.tracks.size());
            for (EntityId id : tracked_before[gid]) {
                if (!belief.find_track(id))
                    result.stats.tracks_expired++;
            }
        }
        result.stats.tracks_active = total_active;

        // World hash every 10 ticks
        if (tick % 10 == 0)
            result.world_hashes.push_back(compute_world_hash(entities, beliefs));
    }

    result.final_track_count = 0;
    for (const auto& [gid, belief] : beliefs)
        result.final_track_count += static_cast<int>(belief.tracks.size());
    return result;
}
