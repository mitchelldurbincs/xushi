#pragma once

#include "belief.h"
#include "grid.h"

#include <cassert>
#include <vector>

// Debug invariant checks. Compile away in release builds (NDEBUG defined).

#ifndef NDEBUG

inline void check_confidence_range(float c, const char* context) {
    assert(c >= 0.0f && c <= 1.0f && "confidence out of [0,1] range");
    (void)context;
}

inline void check_uncertainty_non_negative(float u, const char* context) {
    assert(u >= 0.0f && "negative uncertainty");
    (void)context;
}

inline void check_belief_invariants(const BeliefState& bs, const char* context) {
    for (const auto& t : bs.tracks) {
        check_confidence_range(t.confidence, context);
        check_uncertainty_non_negative(t.uncertainty, context);
        assert(t.status != TrackStatus::EXPIRED && "expired track not removed");
        assert(t.class_id >= 0 && "negative class_id");
    }
    (void)context;
}

#else

inline void check_confidence_range(float, const char*) {}
inline void check_uncertainty_non_negative(float, const char*) {}
inline void check_belief_invariants(const BeliefState&, const char*) {}

#endif
