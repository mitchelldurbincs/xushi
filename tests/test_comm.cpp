#include "test_helpers.h"
#include "../src/comm.h"

static MessagePayload make_obs_payload(int tick) {
    MessagePayload p;
    p.type = MessagePayload::OBSERVATION;
    p.observation = {tick, 0, 1, {10, 20}, 1.0f, 0.8f};
    return p;
}

static void test_base_latency_delivery(TestContext& ctx) {
    CommSystem cs;
    Rng rng(1);
    CommChannel ch = {5, 0.0f, 0.0f};
    cs.send(0, 1, make_obs_payload(0), 10, 0.0f, ch, rng);
    std::vector<Message> out;
    cs.deliver(14, out);
    ctx.check(out.empty(), "not delivered before delivery_tick");
    cs.deliver(15, out);
    ctx.check(out.size() == 1, "delivered at send_tick + base_latency");
    ctx.check(out[0].send_tick == 10, "send_tick preserved");
}

static void test_distance_latency(TestContext& ctx) {
    CommSystem cs;
    Rng rng(1);
    CommChannel ch = {0, 0.1f, 0.0f};
    cs.send(0, 1, make_obs_payload(0), 5, 100.0f, ch, rng);
    std::vector<Message> out;
    cs.deliver(14, out);
    ctx.check(out.empty(), "distance latency: not early");
    cs.deliver(15, out);
    ctx.check(out.size() == 1, "distance latency: arrives at tick 15");
}

static void test_no_loss(TestContext& ctx) {
    CommSystem cs;
    CommChannel ch = {1, 0.0f, 0.0f};
    for (int i = 0; i < 100; ++i) {
        Rng rng(i);
        cs.send(0, 1, make_obs_payload(0), 0, 0.0f, ch, rng);
    }
    std::vector<Message> out;
    cs.deliver(1, out);
    ctx.check(out.size() == 100, "loss_probability=0 -> all 100 delivered");
}

static void test_total_loss(TestContext& ctx) {
    CommSystem cs;
    CommChannel ch = {1, 0.0f, 1.0f};
    for (int i = 0; i < 100; ++i) {
        Rng rng(i);
        cs.send(0, 1, make_obs_payload(0), 0, 0.0f, ch, rng);
    }
    std::vector<Message> out;
    cs.deliver(1, out);
    ctx.check(out.empty(), "loss_probability=1 -> none delivered");
}

static void test_determinism(TestContext& ctx) {
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
    ctx.check(run(42) == run(42), "same seed -> same delivery count");
    ctx.check(run(42) != run(999), "different seeds -> different delivery pattern");
}

static void test_multiple_deliveries_same_tick(TestContext& ctx) {
    CommSystem cs;
    Rng rng(1);
    CommChannel ch = {3, 0.0f, 0.0f};
    cs.send(0, 1, make_obs_payload(0), 10, 0.0f, ch, rng);
    cs.send(2, 1, make_obs_payload(0), 10, 0.0f, ch, rng);
    cs.send(3, 1, make_obs_payload(0), 10, 0.0f, ch, rng);
    std::vector<Message> out;
    cs.deliver(13, out);
    ctx.check(out.size() == 3, "three messages delivered on same tick");
}

static void test_independent_receivers(TestContext& ctx) {
    CommSystem cs;
    Rng rng(1);
    CommChannel ch = {2, 0.0f, 0.0f};
    cs.send(0, 1, make_obs_payload(0), 5, 0.0f, ch, rng);
    cs.send(0, 2, make_obs_payload(0), 5, 0.0f, ch, rng);
    std::vector<Message> out;
    cs.deliver(7, out);
    ctx.check(out.size() == 2, "messages to different receivers both arrive");
    ctx.check(out[0].receiver == 1 && out[1].receiver == 2, "receivers correct");
}

int main() {
    return run_test_suite("comm", [](TestContext& ctx) {
    test_base_latency_delivery(ctx);
    test_distance_latency(ctx);
    test_no_loss(ctx);
    test_total_loss(ctx);
    test_determinism(ctx);
    test_multiple_deliveries_same_tick(ctx);
    test_independent_receivers(ctx);
    });
}
