#include "types.h"
#include "map.h"
#include "sensing.h"
#include "comm.h"
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

    // Radio channel: 3 tick latency, no distance cost, 10% loss
    CommChannel radio = {3, 0.0f, 0.1f};
    CommSystem comms;

    for (int tick = 0; tick < 60; ++tick) {
        // Movement
        target.position = target.position + target.velocity * dt;

        // Sensing
        Observation obs{};
        bool detected = sense(map, drone.position, drone.id,
                              target.position, target.id,
                              max_range, tick, rng, obs);

        // Drone sends observation to ground team when it detects
        if (detected) {
            float dist = (ground.position - drone.position).length();
            MessagePayload payload;
            payload.type = MessagePayload::OBSERVATION;
            payload.observation = obs;
            comms.send(drone.id, ground.id, payload, tick, dist, radio, rng);
        }

        // Deliver messages due this tick
        std::vector<Message> delivered;
        comms.deliver(tick, delivered);

        // Print
        if (detected) {
            std::printf("tick %3d  DRONE sees target at (%5.1f,%5.1f)\n",
                        tick, obs.estimated_position.x, obs.estimated_position.y);
        } else {
            std::printf("tick %3d  DRONE ---\n", tick);
        }

        for (const auto& msg : delivered) {
            const auto& o = msg.payload.observation;
            std::printf("         GROUND receives report from tick %d: "
                        "target at (%5.1f,%5.1f)\n",
                        msg.send_tick,
                        o.estimated_position.x, o.estimated_position.y);
        }
    }

    return 0;
}
