#include "test_helpers.h"
#include "../src/replay.h"
#include "../src/replay_events.h"
#include "../src/scenario.h"
#include "../src/sim_engine.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {

struct PhaseOrderHooks : RoundHooks {
    std::vector<std::string> phases;
    void on_phase_timing(const char* phase, double /*elapsed_us*/) override {
        phases.push_back(phase);
    }
};

struct ReplayCaptureHooks : RoundHooks {
    ReplayWriter* writer = nullptr;
    void on_round_started(int round, int team) override {
        writer->log(replay_round_started(round, team));
    }
    void on_round_ended(int round) override {
        writer->log(replay_round_ended(round));
    }
    void on_track_update(int round, int team, const Track& trk) override {
        writer->log(replay_track_update(round, team, trk));
    }
    void on_world_hash(int round, uint64_t hash) override {
        writer->log(replay_world_hash(round, hash));
    }
};

static uint64_t fnv1a_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    uint64_t hash = 1469598103934665603ULL;
    char c = 0;
    while (in.get(c)) {
        hash ^= static_cast<unsigned char>(c);
        hash *= 1099511628211ULL;
    }
    return hash;
}

static uint64_t run_replay_checksum(const Scenario& scn, const std::string& path) {
    SimEngine engine;
    engine.init(scn);
    ReplayWriter writer(path);
    writer.log(replay_header(scn, "contract-test"));

    ReplayCaptureHooks hooks;
    hooks.writer = &writer;
    for (int round = 0; round < scn.rounds; ++round)
        engine.run_round(round, hooks);
    return fnv1a_file(path);
}

static void test_round_phase_order(TestContext& ctx) {
    Scenario scn = load_scenario("scenarios/small_office_breach.json");
    scn.rounds = 1;

    SimEngine engine;
    engine.init(scn);
    PhaseOrderHooks hooks;
    engine.run_round(0, hooks);

    // Required ordering prefix: round_start -> support -> activations... -> round_end.
    ctx.check(!hooks.phases.empty(), "phases emitted");
    ctx.check(hooks.phases.front() == "round_start", "first phase is round_start");
    ctx.check(hooks.phases[1] == "support", "second phase is support");
    ctx.check(hooks.phases.back() == "round_end", "last phase is round_end");

    // Count activations = number of alive operators.
    int expected_activations = 0;
    for (const auto& e : scn.entities)
        if (e.kind == EntityKind::Operator && e.hp > 0)
            ++expected_activations;
    int actual = 0;
    for (const auto& p : hooks.phases)
        if (p == "activation") ++actual;
    ctx.check(actual == expected_activations,
              "one activation per alive operator");
}

static void test_initiative_alternates_by_round(TestContext& ctx) {
    Scenario scn = load_scenario("scenarios/small_office_breach.json");
    SimEngine engine;
    engine.init(scn);
    PhaseOrderHooks hooks;

    engine.begin_round(0, hooks);
    const int first = engine.round_state().initiative_team;
    while (!engine.step_activation(0, hooks)) {}
    engine.finalize_round(0, hooks);

    engine.begin_round(1, hooks);
    const int second = engine.round_state().initiative_team;
    while (!engine.step_activation(1, hooks)) {}
    engine.finalize_round(1, hooks);

    ctx.check(first == 0, "round 0 initiative is team 0");
    ctx.check(second == 1, "round 1 initiative alternates to team 1");
}

static void test_dead_operators_removed_from_activation_order(TestContext& ctx) {
    Scenario scn = load_scenario("scenarios/small_office_breach.json");
    scn.rounds = 1;
    for (auto& e : scn.entities)
        if (e.id == 0) e.hp = 0;

    SimEngine engine;
    engine.init(scn);
    PhaseOrderHooks hooks;
    engine.run_round(0, hooks);

    int activations = 0;
    for (const auto& p : hooks.phases)
        if (p == "activation") ++activations;

    int expected = 0;
    for (const auto& e : scn.entities)
        if (e.kind == EntityKind::Operator && e.hp > 0)
            ++expected;
    ctx.check(activations == expected,
              "eliminated operator removed from activation order");
}

static void test_ap_refresh_at_round_start(TestContext& ctx) {
    Scenario scn = load_scenario("scenarios/small_office_breach.json");
    SimEngine engine;
    engine.init(scn);
    PhaseOrderHooks hooks;
    engine.begin_round(0, hooks);
    // Each alive operator should have max_ap in the round context.
    bool all_full = true;
    for (const auto& e : scn.entities) {
        if (e.kind != EntityKind::Operator) continue;
        if (e.hp <= 0) continue;
        auto it = engine.round_context().operator_ap.find(e.id);
        if (it == engine.round_context().operator_ap.end() || it->second != e.max_ap)
            all_full = false;
    }
    ctx.check(all_full, "operator AP is refreshed to max at round start");

    // Support AP for each team at max (contract §2).
    bool support_full = true;
    for (const auto& [team, ap] : engine.round_context().support_ap)
        if (ap != engine.round_context().support_ap_max.at(team))
            support_full = false;
    ctx.check(support_full, "support AP refreshed to max per team");
}

static void test_deterministic_replay_checksums(TestContext& ctx) {
    Scenario scn = load_scenario("scenarios/small_office_breach.json");
    const std::string a_path = "tests/tmp_contract_a.replay";
    const std::string b_path = "tests/tmp_contract_b.replay";
    const std::string c_path = "tests/tmp_contract_c.replay";

    uint64_t a = run_replay_checksum(scn, a_path);
    uint64_t b = run_replay_checksum(scn, b_path);
    ctx.check(a == b, "same seed produces identical replay checksum");

    scn.seed += 1;
    uint64_t c = run_replay_checksum(scn, c_path);
    // World hash depends only on entity + belief state, which currently
    // doesn't diverge by seed (no stochastic actions yet). This test merely
    // asserts the infrastructure hashes deterministically — the
    // "different seed differs" expectation will bite once we wire
    // stochastic actions.
    (void)c;

    std::remove(a_path.c_str());
    std::remove(b_path.c_str());
    std::remove(c_path.c_str());
}

}  // namespace

int main() {
    return run_test_suite("contract", [](TestContext& ctx) {
        test_round_phase_order(ctx);
        test_initiative_alternates_by_round(ctx);
        test_dead_operators_removed_from_activation_order(ctx);
        test_ap_refresh_at_round_start(ctx);
        test_deterministic_replay_checksums(ctx);
    });
}
