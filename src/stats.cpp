#include "stats.h"

void SystemStats::print_summary(int total_ticks) const {
    double total_ms = total_us() / 1000.0;
    double ms_per_tick = (total_ticks > 0) ? total_ms / total_ticks : 0;
    double ticks_per_sec = (total_ms > 0) ? (total_ticks * 1000.0) / total_ms : 0;

    std::printf("\n=== Performance Summary ===\n");
    std::printf("ticks:        %d\n", total_ticks);
    std::printf("total:        %.3f ms\n", total_ms);
    std::printf("per tick:     %.3f ms\n", ms_per_tick);
    std::printf("throughput:   %.0f ticks/sec\n", ticks_per_sec);

    std::printf("\n--- System Timing ---\n");
    std::printf("movement:     %.3f ms  (%.1f%%)\n", movement_us / 1000.0,
                total_us() > 0 ? 100.0 * movement_us / total_us() : 0);
    std::printf("sensing:      %.3f ms  (%.1f%%)\n", sensing_us / 1000.0,
                total_us() > 0 ? 100.0 * sensing_us / total_us() : 0);
    std::printf("comm:         %.3f ms  (%.1f%%)\n", comm_us / 1000.0,
                total_us() > 0 ? 100.0 * comm_us / total_us() : 0);
    std::printf("belief:       %.3f ms  (%.1f%%)\n", belief_us / 1000.0,
                total_us() > 0 ? 100.0 * belief_us / total_us() : 0);
    std::printf("replay:       %.3f ms  (%.1f%%)\n", replay_us / 1000.0,
                total_us() > 0 ? 100.0 * replay_us / total_us() : 0);

    std::printf("\n--- Counters ---\n");
    std::printf("rays cast:         %d  (%.1f/tick)\n", rays_cast,
                total_ticks > 0 ? (double)rays_cast / total_ticks : 0);
    std::printf("detections:        %d  (%.1f/tick)\n", detections_generated,
                total_ticks > 0 ? (double)detections_generated / total_ticks : 0);
    std::printf("messages sent:     %d\n", messages_sent);
    std::printf("messages dropped:  %d\n", messages_dropped);
    std::printf("messages delivered: %d\n", messages_delivered);
    std::printf("tracks expired:    %d\n", tracks_expired);
}
