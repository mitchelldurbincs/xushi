#pragma once

#include "observation_state.h"
#include <vector>

struct ObservationPublication {
    EntityId sender = 0;
    SharedObservationPayload payload{};
};

class TruthState {
public:
    void clear() { pending_publications_.clear(); }

    void queue_publication(EntityId sender, const Observation& obs) {
        pending_publications_.push_back(ObservationPublication{sender, to_shared_observation(obs)});
    }

    const std::vector<ObservationPublication>& pending_publications() const {
        return pending_publications_;
    }

private:
    std::vector<ObservationPublication> pending_publications_;
};
