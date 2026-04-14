#pragma once

#include "sensing.h"

// Communication-safe observation payload. This is what can be published
// across agents; it intentionally excludes any direct truth entity handle.
struct SharedObservationPayload {
    int tick = 0;
    EntityId observer = 0;
    EntityId target = 0;
    Vec2 estimated_position{};
    float uncertainty = 0.0f;
    float confidence = 0.0f;
    int class_id = 0;
    float identity_confidence = 0.0f;
    bool is_false_positive = false;
};

inline SharedObservationPayload to_shared_observation(const Observation& obs) {
    SharedObservationPayload shared;
    shared.tick = obs.tick;
    shared.observer = obs.observer;
    shared.target = obs.target;
    shared.estimated_position = obs.estimated_position;
    shared.uncertainty = obs.uncertainty;
    shared.confidence = obs.confidence;
    shared.class_id = obs.class_id;
    shared.identity_confidence = obs.identity_confidence;
    shared.is_false_positive = obs.is_false_positive;
    return shared;
}

inline Observation to_observation(const SharedObservationPayload& shared) {
    Observation obs{};
    obs.tick = shared.tick;
    obs.observer = shared.observer;
    obs.target = shared.target;
    obs.estimated_position = shared.estimated_position;
    obs.uncertainty = shared.uncertainty;
    obs.confidence = shared.confidence;
    obs.class_id = shared.class_id;
    obs.identity_confidence = shared.identity_confidence;
    obs.is_false_positive = shared.is_false_positive;
    return obs;
}
