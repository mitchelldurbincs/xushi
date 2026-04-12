#include "test_helpers.h"
#include "../src/comm.h"

static MessagePayload make_obs_payload(int tick) {
    MessagePayload p;
    p.type = MessagePayload::OBSERVATION;
    p.observation = {tick, 0, 1, {10, 20}, 1.0f, 0.8f};
    return p;
}

static void test_base_latency_delivery() {
    CommSystem cs;
    Rng rng(1);
    CommChannel ch = {5, 0.0f, 0.0f};
    cs.send(0, 1, make_obs_payload(0), 10, 0.0f, ch, rng);
    std::vector<Message> out;
    cs.deliver(14, out);
    CHECK(out.empty(), "not delivered before delivery_tick");
    cs.deliver(15, out);
    CHECK(out.size() == 1, "delivered at send_tick + base_latency");
    CHECK(out[0].send_tick == 10, "send_tick preserved");
}

static void test_distance_latency() {
    CommSystem cs;
    Rng rng(1);
    CommChannel ch = {0, 0.1f, 0.0f};
    cs.send(0, 1, make_obs_payload(0), 5, 100.0f, ch, rng);
    std::vector<Message> out;
    cs.deliver(14, out);
    CHECK(out.empty(), "distance latency: not early");
    cs.deliver(15, out);
    CHECK(out.size() == 1, "distance latency: arrives at tick 15");
}

static void test_no_loss() {
    CommSystem cs;
    CommChannel ch = {1, 0.0f, 0.0f};
    for (int i = 0; i < 100; ++i) {
        Rng rng(i);
        cs.send(0, 1, make_obs_payload(0), 0, 0.0f, ch, rng);
    }
    std::vector<Message> out;
    cs.deliver(1, out);
    CHECK(out.size() == 100, "loss_probability=0 -> all 100 delivered");
}

static void test_total_loss() {
    CommSystem cs;
    CommChannel ch = {1, 0.0f, 1.0f};
    for (int i = 0; i < 100; ++i) {
        Rng rng(i);
        cs.send(0, 1, make_obs_payload(0), 0, 0.0f, ch, rng);
    }
    std::vector<Message> out;
    cs.deliver(1, out);
    CHECK(out.empty(), "loss_probability=1 -> none delivered");
}

static void test_determinism() {
    auto run = [](uint64_t seed) {
        CommSystem cs;
        Rng rng(seed);
        CommChannel ch = {2, 0.0f, 0.3f};
        for (int i = 0; i < 50; ++i)
            cs.send(0, 1, make_obs_payload(i), 0, 0.0f, ch, rng);
        std::vector<Message> out;
        cs.deliver(2, out);
        return out.size();
    };
    CHECK(run(42) == run(42), "same seed -> same delivery count");
    CHECK(run(42) != run(999), "different seeds -> different delivery pattern");
}

static void test_multiple_deliveries_same_tick() {
    CommSystem cs;
    Rng rng(1);
    CommChannel ch = {3, 0.0f, 0.0f};
    cs.send(0, 1, make_obs_payload(0), 10, 0.0f, ch, rng);
    cs.send(2, 1, make_obs_payload(0), 10, 0.0f, ch, rng);
    cs.send(3, 1, make_obs_payload(0), 10, 0.0f, ch, rng);
    std::vector<Message> out;
    cs.deliver(13, out);
    CHECK(out.size() == 3, "three messages delivered on same tick");
}

static void test_independent_receivers() {
    CommSystem cs;
    Rng rng(1);
    CommChannel ch = {2, 0.0f, 0.0f};
    cs.send(0, 1, make_obs_payload(0), 5, 0.0f, ch, rng);
    cs.send(0, 2, make_obs_payload(0), 5, 0.0f, ch, rng);
    std::vector<Message> out;
    cs.deliver(7, out);
    CHECK(out.size() == 2, "messages to different receivers both arrive");
    CHECK(out[0].receiver == 1 && out[1].receiver == 2, "receivers correct");
}

int main() {
    std::printf("Running comm tests...\n");
    test_base_latency_delivery();
    test_distance_latency();
    test_no_loss();
    test_total_loss();
    test_determinism();
    test_multiple_deliveries_same_tick();
    test_independent_receivers();
    TEST_REPORT();
}
