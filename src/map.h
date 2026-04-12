#pragma once

#include "types.h"
#include <vector>

struct Rect {
    Vec2 min;
    Vec2 max;
};

struct Map {
    std::vector<Rect> obstacles;

    // Returns true if the line segment from→to does not intersect any obstacle.
    bool line_of_sight(Vec2 from, Vec2 to) const;
};
