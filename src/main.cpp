#include "types.h"
#include "map.h"
#include "sensing.h"
#include "comm.h"
#include "belief.h"
#include "rng.h"
#include "scenario.h"
#include "replay.h"
#include "replay_events.h"
#include <cstdio>
#include <cstdlib>
#include <string>

struct Entity {
    EntityId id;
    std::string type;
    Vec2 position;
    Vec2 velocity;
};

// Simple world hash: FNV-1a over entity positions and belief tracks.
static uint64_t compute_world_hash(const std::vector<Entity>& entities,
                                    const BeliefState& belief) {
    uint64_t h = 14695981039346656037ULL; // FNV offset basis
    auto mix = [&](const void* data, size_t len) {
        const auto* bytes = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < len; ++i) {
            h ^= bytes[i];
            h *= 1099511628211ULL; // FNV prime
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
    const char* path = (argc > 1) ? argv[1] : "scenarios/default.json";

    Scenario scn;
    try {
        scn = load_scenario(path);
    } catch (const std::runtime_error& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }

    // Derive replay path: replace .json with .replay
    std::string replay_path = path;
    auto dot = replay_path.rfind('.');
    if (dot != std::string::npos)
        replay_path = replay_path.substr(0, dot);
    replay_path += ".replay";

    Map map;
    map.obstacles = scn.obstacles;

    std::vector<Entity> entities;
    for (const auto& se : scn.entities)
        entities.push_back({se.id, se.type, se.position, se.velocity});

    Entity* drone = nullptr;
    Entity* ground = nullptr;
    std::vector<Entity*> targets;
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

    ReplayWriter replay(replay_path);
    replay.log(replay_header(scn, path));

    std::printf("scenario: %s  seed: %llu  ticks: %d  replay: %s\n\n",
                path, static_cast<unsigned long long>(scn.seed), scn.ticks, replay_path.c_str());

    for (int tick = 0; tick < scn.ticks; ++tick) {
        // Movement
        for (auto& e : entities)
            e.position = e.position + e.velocity * scn.dt;

        // Sensing
        for (auto* tgt : targets) {
            Observation obs{};
            bool detected = sense(map, drone->position, drone->id,
                                  tgt->position, tgt->id,
                                  scn.max_sensor_range, tick, rng, obs);

            if (detected) {
                replay.log(replay_detection(tick, obs));

                float dist = (ground->position - drone->position).length();
                MessagePayload payload;
                payload.type = MessagePayload::OBSERVATION;
                payload.observation = obs;

                int delivery_tick = tick + scn.channel.base_latency_ticks;
                bool sent = comms.send(drone->id, ground->id, payload, tick,
                                       dist, scn.channel, rng);
                if (sent) {
                    replay.log(replay_msg_sent(tick, drone->id, ground->id, delivery_tick));
                } else {
                    replay.log(replay_msg_dropped(tick, drone->id, ground->id));
                }

                std::printf("tick %3d  DRONE detected target %u\n", tick, tgt->id);
            } else {
                std::printf("tick %3d  DRONE ---\n", tick);
            }
        }

        // Deliver messages and feed into belief
        std::vector<Message> delivered;
        comms.deliver(tick, delivered);

        // Track which targets had tracks before this tick (for expiry detection)
        std::vector<EntityId> tracked_before;
        for (const auto& t : ground_belief.tracks)
            tracked_before.push_back(t.target);

        for (const auto& msg : delivered) {
            replay.log(replay_msg_delivered(tick, msg.sender, msg.receiver));
            ground_belief.update(msg.payload.observation, tick);
        }

        // Decay belief
        ground_belief.decay(tick, scn.belief);

        // Log track updates and detect expirations
        for (const auto& t : ground_belief.tracks)
            replay.log(replay_track_update(tick, ground->id, t));

        for (EntityId id : tracked_before) {
            if (!ground_belief.find_track(id))
                replay.log(replay_track_expired(tick, ground->id, id));
        }

        // Print ground belief
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

        // World hash every 10 ticks
        if (tick % 10 == 0) {
            uint64_t h = compute_world_hash(entities, ground_belief);
            replay.log(replay_world_hash(tick, h));
        }
    }

    replay.close();
    return 0;
}
