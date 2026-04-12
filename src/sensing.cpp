#include "sensing.h"

bool sense(const Map& map,
           Vec2 observer_pos, EntityId observer_id,
           Vec2 target_pos, EntityId target_id,
           float max_range, int tick,
           Rng& rng, Observation& out) {

    Vec2 diff = target_pos - observer_pos;
    float range = diff.length();

    if (range > max_range)
        return false;

    if (!map.line_of_sight(observer_pos, target_pos))
        return false;

    // Noise scales linearly with range fraction
    float range_frac = range / max_range;
    float noise_stddev = range_frac * 2.0f; // up to 2m stddev at max range

    Vec2 noise = {rng.normal() * noise_stddev, rng.normal() * noise_stddev};

    out.tick = tick;
    out.observer = observer_id;
    out.target = target_id;
    out.estimated_position = target_pos + noise;
    out.uncertainty = noise_stddev;
    out.confidence = 1.0f - range_frac;

    return true;
}
