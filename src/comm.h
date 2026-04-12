#pragma once

#include "types.h"
#include "sensing.h"
#include "rng.h"
#include <vector>

struct MessagePayload {
    enum Type { OBSERVATION };
    Type type;
    Observation observation; // valid when type == OBSERVATION
};

struct Message {
    EntityId sender;
    EntityId receiver;
    int send_tick;
    int delivery_tick;
    MessagePayload payload;
};

struct CommChannel {
    int base_latency_ticks;      // fixed minimum delay
    float latency_per_distance;  // extra ticks per meter of sender-receiver distance
    float loss_probability;      // chance message is dropped entirely [0, 1]
};

struct CommSystem {
    std::vector<Message> pending;

    // Queue a message. Computes delivery_tick from channel config and distance.
    // May drop the message based on loss_probability.
    void send(EntityId sender, EntityId receiver,
              const MessagePayload& payload, int current_tick,
              float distance, const CommChannel& channel, Rng& rng);

    // Collect and remove all messages whose delivery_tick == current_tick.
    void deliver(int current_tick, std::vector<Message>& out);
};
