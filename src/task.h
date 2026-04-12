#pragma once

#include "types.h"

struct Task {
    enum class Type { VERIFY };
    Type type;
    EntityId assigned_to;
    EntityId target_id;
    Vec2 target_position;
    int assigned_tick;
};
