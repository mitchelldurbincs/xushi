#include "types.h"
#include "map.h"
#include "sensing.h"
#include "comm.h"
#include "belief.h"
#include "rng.h"
#include <cstdio>

struct Entity {
    EntityId id;
    Vec2 position;
    Vec2 velocity;
};

int main() {
    Map map;
    map.obstacles.push_back({{45.0f, 15.0f}, {55.0f, 25.0f}});

    Entity drone  = {0, {50.0f, 0.0f},  {0.0f, 0.0f}};
    Entity ground = {1, {50.0f, 50.0f}, {0.0f, 0.0f}};
    Entity target = {2, {20.0f, 30.0f}, {1.0f, 0.0f}};

    constexpr float dt = 1.0f;
    constexpr float max_range = 80.0f;
    Rng rng(12345);

    CommChannel radio = {3, 0.0f, 0.1f};
    CommSystem comms;
    BeliefState ground_belief;
    BeliefConfig belief_cfg;

    for (int tick = 0; tick < 60; ++tick) {
        // Movement
        target.position = target.position + target.velocity * dt;

        // Sensing
        Observation obs{};
        bool detected = sense(map, drone.position, drone.id,
                              target.position, target.id,
                              max_range, tick, rng, obs);

        // Drone sends observation to ground team
        if (detected) {
            float dist = (ground.position - drone.position).length();
            MessagePayload payload;
            payload.type = MessagePayload::OBSERVATION;
            payload.observation = obs;
            comms.send(drone.id, ground.id, payload, tick, dist, radio, rng);
        }

        // Deliver messages and feed into belief
        std::vector<Message> delivered;
        comms.deliver(tick, delivered);
        for (const auto& msg : delivered) {
            ground_belief.update(msg.payload.observation, tick);
        }

        // Decay belief
        ground_belief.decay(tick, belief_cfg);

        // Print drone status
        if (detected) {
            std::printf("tick %3d  DRONE detected target\n", tick);
        } else {
            std::printf("tick %3d  DRONE ---\n", tick);
        }

        // Print ground belief
        const Track* trk = ground_belief.find_track(target.id);
        if (trk) {
            int age = tick - trk->last_update_tick;
            std::printf("         GROUND belief: target at (%5.1f,%5.1f)  "
                        "conf:%.2f  unc:%.1f  age:%d  [%s]\n",
                        trk->estimated_position.x, trk->estimated_position.y,
                        trk->confidence, trk->uncertainty, age,
                        track_status_str(trk->status));
        } else {
            std::printf("         GROUND belief: no track\n");
        }
    }

    return 0;
}
