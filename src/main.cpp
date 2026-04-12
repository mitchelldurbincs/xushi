#include "types.h"
#include "map.h"
#include "sensing.h"
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

    Entity drone = {0, {50.0f, 0.0f}, {0.0f, 0.0f}};
    Entity target = {1, {20.0f, 30.0f}, {1.0f, 0.0f}};

    constexpr float dt = 1.0f;
    constexpr float max_range = 80.0f;
    Rng rng(12345);

    for (int tick = 0; tick < 100; ++tick) {
        target.position = target.position + target.velocity * dt;

        Observation obs{};
        bool detected = sense(map, drone.position, drone.id,
                              target.position, target.id,
                              max_range, tick, rng, obs);

        if (detected) {
            std::printf("tick %3d  DETECTED  est:(%5.1f,%5.1f)  "
                        "true:(%5.1f,%5.1f)  conf:%.2f  unc:%.2f\n",
                        tick,
                        obs.estimated_position.x, obs.estimated_position.y,
                        target.position.x, target.position.y,
                        obs.confidence, obs.uncertainty);
        } else {
            std::printf("tick %3d  ---\n", tick);
        }
    }

    return 0;
}
