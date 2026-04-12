#include "sim.h"
#include "map.h"
#include "sensing.h"
#include "comm.h"
#include "belief.h"
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

    std::vector<ScenarioEntity*> drones;
    std::vector<ScenarioEntity*> grounds;
    std::vector<ScenarioEntity*> targets;
    for (auto& e : entities) {
        if (e.role == ScenarioEntity::Role::Drone)  drones.push_back(&e);
        if (e.role == ScenarioEntity::Role::Ground) grounds.push_back(&e);
        if (e.role == ScenarioEntity::Role::Target) targets.push_back(&e);
    }

    if (drones.empty() || grounds.empty() || targets.empty())
        return result;

    Rng rng(scn.seed);
    CommSystem comms;
    std::map<EntityId, BeliefState> beliefs;
    for (auto* g : grounds)
        beliefs[g->id] = BeliefState{};

    for (int tick = 0; tick < scn.ticks; ++tick) {
        // Movement
        for (auto& e : entities)
            e.position = e.position + e.velocity * scn.dt;

        // Sensing — each drone senses all targets, broadcasts to all grounds
        for (auto* drone : drones) {
            for (auto* tgt : targets) {
                result.stats.sensors_updated++;
                result.stats.rays_cast++;

                Observation obs{};
                bool detected = sense(map, drone->position, drone->id,
                                      tgt->position, tgt->id,
                                      scn.max_sensor_range, tick, rng, obs);

                if (detected) {
                    result.stats.detections_generated++;

                    MessagePayload payload;
                    payload.type = MessagePayload::OBSERVATION;
                    payload.observation = obs;

                    for (auto* ground : grounds) {
                        float dist = (ground->position - drone->position).length();
                        int dt = comms.send(drone->id, ground->id, payload, tick,
                                            dist, scn.channel, rng);
                        if (dt >= 0)
                            result.stats.messages_sent++;
                        else
                            result.stats.messages_dropped++;
                    }
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
