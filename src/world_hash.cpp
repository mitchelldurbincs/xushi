#include "world_hash.h"

#include <cstring>

uint64_t compute_world_hash_canonical(const std::vector<ScenarioEntity>& entities,
                                      const std::map<int, BeliefState>& beliefs) {
    uint64_t h = 14695981039346656037ULL;
    auto mix = [&](const void* data, size_t len) {
        const auto* bytes = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < len; ++i) {
            h ^= bytes[i];
            h *= 1099511628211ULL;
        }
    };
    auto mix_int = [&](int32_t v) { mix(&v, sizeof(v)); };
    auto mix_gridpos = [&](GridPos p) {
        mix_int(static_cast<int32_t>(p.x));
        mix_int(static_cast<int32_t>(p.y));
    };
    auto mix_float = [&](float f) {
        uint32_t bits;
        std::memcpy(&bits, &f, sizeof(bits));
        mix(&bits, sizeof(bits));
    };

    for (const auto& e : entities) {
        mix(&e.id, sizeof(e.id));
        mix_gridpos(e.pos);
        mix_int(e.hp);
        mix_int(e.ammo);
        mix_int(static_cast<int>(e.kind));
        mix_int(e.team);
        mix_int(e.drone_battery);
    }

    for (const auto& [team, belief] : beliefs) {
        mix_int(team);
        for (const auto& t : belief.tracks) {
            mix(&t.target, sizeof(t.target));
            mix_gridpos(t.estimated_position);
            mix_float(t.confidence);
            mix_float(t.uncertainty);
            mix_int(t.class_id);
            mix(&t.source_mask, sizeof(t.source_mask));
            mix_int(static_cast<int>(t.status));
        }
    }
    return h;
}
