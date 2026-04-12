#include "map.h"
#include <algorithm>
#include <cmath>

// Segment-vs-AABB intersection using the slab method.
// Returns true if the segment from→to intersects the rectangle.
static bool segment_intersects_rect(Vec2 from, Vec2 to, const Rect& rect) {
    Vec2 d = to - from;

    float tmin = 0.0f;
    float tmax = 1.0f;

    // X slab
    if (std::fabs(d.x) < 1e-9f) {
        // Ray is parallel to X slab — check if origin is inside
        if (from.x < rect.min.x || from.x > rect.max.x)
            return false;
    } else {
        float inv = 1.0f / d.x;
        float t1 = (rect.min.x - from.x) * inv;
        float t2 = (rect.max.x - from.x) * inv;
        if (t1 > t2) std::swap(t1, t2);
        tmin = std::max(tmin, t1);
        tmax = std::min(tmax, t2);
        if (tmin > tmax) return false;
    }

    // Y slab
    if (std::fabs(d.y) < 1e-9f) {
        if (from.y < rect.min.y || from.y > rect.max.y)
            return false;
    } else {
        float inv = 1.0f / d.y;
        float t1 = (rect.min.y - from.y) * inv;
        float t2 = (rect.max.y - from.y) * inv;
        if (t1 > t2) std::swap(t1, t2);
        tmin = std::max(tmin, t1);
        tmax = std::min(tmax, t2);
        if (tmin > tmax) return false;
    }

    return true;
}

bool Map::line_of_sight(Vec2 from, Vec2 to) const {
    for (const auto& obs : obstacles) {
        if (segment_intersects_rect(from, to, obs))
            return false;
    }
    return true;
}
