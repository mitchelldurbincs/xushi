#include "sim.h"
#include "map.h"
#include "sensing.h"
#include "comm.h"
#include "belief.h"
#include "rng.h"

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

SimResult run_scenario_headless(const Scenario& scn) {
    SimResult result{};

    Map map;
    map.obstacles = scn.obstacles;

    std::vector<ScenarioEntity> entities = scn.entities;

    ScenarioEntity* drone = nullptr;
    ScenarioEntity* ground = nullptr;
    std::vector<ScenarioEntity*> targets;
    for (auto& e : entities) {
        if (e.role == ScenarioEntity::Role::Drone)  drone = &e;
        if (e.role == ScenarioEntity::Role::Ground) ground = &e;
        if (e.role == ScenarioEntity::Role::Target) targets.push_back(&e);
    }

    if (!drone || !ground || targets.empty())
        return result;

    Rng rng(scn.seed);
    CommSystem comms;
    BeliefState ground_belief;

    for (int tick = 0; tick < scn.ticks; ++tick) {
        // Movement
        for (auto& e : entities)
            e.position = e.position + e.velocity * scn.dt;

        // Sensing
        for (auto* tgt : targets) {
            result.stats.sensors_updated++;
            result.stats.rays_cast++;

            Observation obs{};
            bool detected = sense(map, drone->position, drone->id,
                                  tgt->position, tgt->id,
                                  scn.max_sensor_range, tick, rng, obs);

            if (detected) {
                result.stats.detections_generated++;

                float dist = (ground->position - drone->position).length();
                MessagePayload payload;
                payload.type = MessagePayload::OBSERVATION;
                payload.observation = obs;

                int dt = comms.send(drone->id, ground->id, payload, tick,
                                    dist, scn.channel, rng);
                if (dt >= 0)
                    result.stats.messages_sent++;
                else
                    result.stats.messages_dropped++;
            }
        }

        // Communication
        std::vector<Message> delivered;
        comms.deliver(tick, delivered);

        // Track expirations
        std::vector<EntityId> tracked_before;
        for (const auto& t : ground_belief.tracks)
            tracked_before.push_back(t.target);

        // Belief
        for (const auto& msg : delivered) {
            result.stats.messages_delivered++;
            ground_belief.update(msg.payload.observation, tick);
        }
        ground_belief.decay(tick, scn.dt, scn.belief);

        result.stats.tracks_active = static_cast<int>(ground_belief.tracks.size());
        for (EntityId id : tracked_before) {
            if (!ground_belief.find_track(id))
                result.stats.tracks_expired++;
        }

        // World hash every 10 ticks
        if (tick % 10 == 0)
            result.world_hashes.push_back(compute_world_hash(entities, ground_belief));
    }

    result.final_track_count = static_cast<int>(ground_belief.tracks.size());
    return result;
}
