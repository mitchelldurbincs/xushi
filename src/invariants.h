#pragma once

#include "types.h"
#include "belief.h"
#include <cassert>
#include <cmath>
#include <vector>

// Debug invariant checks. Compile away in release builds (NDEBUG defined).

#ifndef NDEBUG

inline void check_finite(Vec2 v, const char* context) {
    assert(!std::isnan(v.x) && !std::isinf(v.x) && "NaN/Inf in x coordinate");
    assert(!std::isnan(v.y) && !std::isinf(v.y) && "NaN/Inf in y coordinate");
    (void)context;
}

inline void check_confidence_range(float c, const char* context) {
    assert(c >= 0.0f && c <= 1.0f && "confidence out of [0,1] range");
    (void)context;
}

inline void check_uncertainty_positive(float u, const char* context) {
    assert(u >= 0.0f && "negative uncertainty");
    assert(!std::isnan(u) && !std::isinf(u) && "NaN/Inf in uncertainty");
    (void)context;
}

inline void check_belief_invariants(const BeliefState& bs, const char* context) {
    for (const auto& t : bs.tracks) {
        check_finite(t.estimated_position, context);
        check_confidence_range(t.confidence, context);
        check_uncertainty_positive(t.uncertainty, context);
        assert(t.status != TrackStatus::EXPIRED && "expired track not removed");
        assert(t.identity_confidence >= 0.0f && t.identity_confidence <= 1.0f
               && "identity_confidence out of [0,1] range");
        assert(t.corroboration_count >= 0 && "negative corroboration_count");
        assert(t.class_id >= 0 && "negative class_id");
    }
    (void)context;
}

template <typename EntityContainer>
inline void check_positions_finite(const EntityContainer& entities, const char* context) {
    for (const auto& e : entities) {
        check_finite(e.position, context);
    }
    (void)context;
}

#else

inline void check_finite(Vec2, const char*) {}
inline void check_confidence_range(float, const char*) {}
inline void check_uncertainty_positive(float, const char*) {}
inline void check_belief_invariants(const BeliefState&, const char*) {}
template <typename T>
inline void check_positions_finite(const T&, const char*) {}

#endif
