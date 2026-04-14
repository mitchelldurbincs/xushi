#include "world_hash.h"

uint64_t compute_world_hash_canonical(const std::vector<ScenarioEntity>& entities,
                                      const std::map<EntityId, BeliefState>& beliefs) {
    uint64_t h = 14695981039346656037ULL;
    auto mix = [&](const void* data, size_t len) {
        const auto* bytes = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < len; ++i) {
            h ^= bytes[i];
            h *= 1099511628211ULL;
        }
    };

    // Entities: identity then kinematics.
    for (const auto& e : entities) {
        mix(&e.id, sizeof(e.id));
        mix(&e.position.x, sizeof(float));
        mix(&e.position.y, sizeof(float));
        mix(&e.current_waypoint, sizeof(e.current_waypoint));
    }

    // Beliefs/tracks: include identity/corroboration fields used by gating.
    for (const auto& [owner_id, belief] : beliefs) {
        mix(&owner_id, sizeof(owner_id));
        for (const auto& t : belief.tracks) {
            mix(&t.target, sizeof(t.target));
            mix(&t.estimated_position.x, sizeof(float));
            mix(&t.estimated_position.y, sizeof(float));
            mix(&t.confidence, sizeof(float));
            mix(&t.uncertainty, sizeof(float));
            mix(&t.class_id, sizeof(t.class_id));
            mix(&t.identity_confidence, sizeof(float));
            mix(&t.corroboration_count, sizeof(t.corroboration_count));
            mix(&t.source_mask, sizeof(t.source_mask));
        }
    }

    return h;
}
