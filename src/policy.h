#pragma once

#include "types.h"
#include "belief.h"
#include <optional>

struct Policy {
    virtual ~Policy() = default;

    // Called each tick for entities with can_sense.
    // Returns a desired move target, or nullopt to use default movement.
    virtual std::optional<Vec2> get_move_target(EntityId id,
                                                 const BeliefState& belief,
                                                 int tick) = 0;
};

struct NullPolicy : Policy {
    std::optional<Vec2> get_move_target(EntityId, const BeliefState&, int) override {
        return std::nullopt;
    }
};
