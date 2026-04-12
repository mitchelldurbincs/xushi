#include "comm.h"
#include <cmath>

int CommSystem::send(EntityId sender, EntityId receiver,
                     const MessagePayload& payload, int current_tick,
                     float distance, const CommChannel& channel, Rng& rng) {
    // Check for loss
    if (rng.uniform() < channel.loss_probability)
        return -1;

    int distance_latency = static_cast<int>(
        std::ceil(distance * channel.latency_per_distance));
    int delivery_tick = current_tick + channel.base_latency_ticks + distance_latency;

    pending.push_back({sender, receiver, current_tick, delivery_tick, payload});
    return delivery_tick;
}

void CommSystem::deliver(int current_tick, std::vector<Message>& out) {
    // Stable partition: move due messages to the end, then pop them.
    // This preserves insertion order among remaining messages.
    size_t write = 0;
    for (size_t i = 0; i < pending.size(); ++i) {
        if (pending[i].delivery_tick == current_tick) {
            out.push_back(pending[i]);
        } else {
            if (write != i)
                pending[write] = pending[i];
            write++;
        }
    }
    pending.resize(write);
}
