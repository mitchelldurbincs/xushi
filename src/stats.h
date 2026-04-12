#pragma once

#include <cstdio>

struct SystemStats {
    // Per-tick counters (accumulated across all ticks)
    int sensors_updated = 0;
    int detections_generated = 0;
    int rays_cast = 0;
    int messages_sent = 0;
    int messages_dropped = 0;
    int messages_delivered = 0;
    int tracks_active = 0;
    int tracks_expired = 0;

    // Per-tick timing (accumulated microseconds)
    double movement_us = 0;
    double sensing_us = 0;
    double comm_us = 0;
    double belief_us = 0;
    double replay_us = 0;

    double total_us() const {
        return movement_us + sensing_us + comm_us + belief_us + replay_us;
    }

    void print_summary(int total_ticks) const;
};
