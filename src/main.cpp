#include "types.h"
#include "map.h"
#include "sensing.h"
#include "comm.h"
#include "belief.h"
#include "rng.h"
#include "scenario.h"
#include <cstdio>
#include <cstdlib>

struct Entity {
    EntityId id;
    std::string type;
    Vec2 position;
    Vec2 velocity;
};

int main(int argc, char* argv[]) {
    const char* path = (argc > 1) ? argv[1] : "scenarios/default.json";

    Scenario scn;
    try {
        scn = load_scenario(path);
    } catch (const std::runtime_error& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }

    // Build map
    Map map;
    map.obstacles = scn.obstacles;

    // Spawn entities
    std::vector<Entity> entities;
    for (const auto& se : scn.entities) {
        entities.push_back({se.id, se.type, se.position, se.velocity});
    }

    // Find entities by type
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

    std::printf("scenario: %s  seed: %llu  ticks: %d\n\n", path,
                static_cast<unsigned long long>(scn.seed), scn.ticks);

    for (int tick = 0; tick < scn.ticks; ++tick) {
        // Movement
        for (auto& e : entities) {
            e.position = e.position + e.velocity * scn.dt;
        }

        // Sensing: drone checks all targets
        for (auto* tgt : targets) {
            Observation obs{};
            bool detected = sense(map, drone->position, drone->id,
                                  tgt->position, tgt->id,
                                  scn.max_sensor_range, tick, rng, obs);

            if (detected) {
                float dist = (ground->position - drone->position).length();
                MessagePayload payload;
                payload.type = MessagePayload::OBSERVATION;
                payload.observation = obs;
                comms.send(drone->id, ground->id, payload, tick, dist, scn.channel, rng);

                std::printf("tick %3d  DRONE detected target %u\n", tick, tgt->id);
            } else {
                std::printf("tick %3d  DRONE ---\n", tick);
            }
        }

        // Deliver messages and feed into belief
        std::vector<Message> delivered;
        comms.deliver(tick, delivered);
        for (const auto& msg : delivered) {
            ground_belief.update(msg.payload.observation, tick);
        }

        // Decay belief
        ground_belief.decay(tick, scn.belief);

        // Print ground belief for each target
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

    return 0;
}
