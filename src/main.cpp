#include "types.h"
#include "map.h"
#include <cstdio>

struct Entity {
    EntityId id;
    Vec2 position;
    Vec2 velocity;
};

int main() {
    // Build a simple map: one building in the middle of the street
    Map map;
    map.obstacles.push_back({{45.0f, 15.0f}, {55.0f, 25.0f}});

    // Drone hovers south of the building
    Entity drone = {0, {50.0f, 0.0f}, {0.0f, 0.0f}};

    // Target walks east across the map at y=30, north of the building.
    // Sightline from drone (50,0) to target must pass through building y=15..25.
    // At speed 1.0 m/tick, target crosses behind the building mid-run.
    Entity target = {1, {20.0f, 30.0f}, {1.0f, 0.0f}};

    constexpr float dt = 1.0f;

    for (int tick = 0; tick < 100; ++tick) {
        // Movement
        target.position = target.position + target.velocity * dt;

        // LOS check
        bool los = map.line_of_sight(drone.position, target.position);

        std::printf("tick %3d  target: (%5.1f, %4.1f)  LOS: %s\n",
                    tick, target.position.x, target.position.y,
                    los ? "YES" : " NO");
    }

    return 0;
}
