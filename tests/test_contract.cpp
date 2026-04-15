#include "test_helpers.h"
#include "../src/action.h"
#include "../src/replay.h"
#include "../src/replay_events.h"
#include "../src/scenario.h"
#include "../src/sim_engine.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {

struct PhaseOrderHooks : TickHooks {
    std::vector<std::string> phases;
    void on_phase_timing(const char* phase, double /*elapsed_us*/) override {
        phases.push_back(phase);
    }
};

struct ActionProbeHooks : TickHooks {
    std::vector<ActionResult> actions;
    std::vector<EffectOutcome> effects;
    void on_action_resolved(int /*tick*/, const ActionResult& result) override { actions.push_back(result); }
    void on_effect_resolved(int /*tick*/, const EffectOutcome& outcome) override { effects.push_back(outcome); }
};

struct MsgProbeHooks : TickHooks {
    std::vector<std::pair<EntityId, EntityId>> delivered;
    void on_msg_delivered(int /*tick*/, EntityId sender, EntityId receiver) override {
        delivered.push_back({sender, receiver});
    }
};

struct ReplayCaptureHooks : TickHooks {
    ReplayWriter* writer = nullptr;
    void on_detection(int tick, const Observation& obs) override { writer->log(replay_detection(tick, obs)); }
    void on_msg_sent(int tick, EntityId sender, EntityId receiver, int delivery_tick) override {
        writer->log(replay_msg_sent(tick, sender, receiver, delivery_tick));
    }
    void on_msg_delivered(int tick, EntityId sender, EntityId receiver) override {
        writer->log(replay_msg_delivered(tick, sender, receiver));
    }
    void on_track_update(int tick, EntityId owner, const Track& trk) override { writer->log(replay_track_update(tick, owner, trk)); }
    void on_action_resolved(int tick, const ActionResult& result) override { writer->log(replay_action_resolved(tick, result)); }
    void on_effect_resolved(int tick, const EffectOutcome& outcome) override { writer->log(replay_effect_resolved(tick, outcome)); }
    void on_world_hash(int tick, uint64_t hash) override { writer->log(replay_world_hash(tick, hash)); }
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

    for (int tick = 0; tick < scn.ticks; ++tick)
        engine.step(tick, hooks);

    return fnv1a_file(path);
}

static void test_round_phase_order(TestContext& ctx) {
    Scenario scn = load_scenario("scenarios/mvp_contract_2v2.json");
    scn.ticks = 1;

    SimEngine engine;
    engine.init(scn);

    PhaseOrderHooks hooks;
    engine.step(0, hooks);

    std::vector<std::string> expected = {"cooldowns"};
    for (size_t i = 0; i < scn.entities.size(); ++i)
        expected.push_back("activation");
    expected.insert(expected.end(), {
        "support_publication_gate", "communication",
        "belief", "reaction_resolution", "tasks", "periodic_snapshots"
    });

    ctx.check(hooks.phases == expected, "round phases execute in deterministic contract order");
}

static void test_ap_spending_and_reaction_trigger(TestContext& ctx) {
    Scenario scn = load_scenario("scenarios/mvp_reaction_ap.json");

    SimEngine engine;
    engine.init(scn);

    ActionProbeHooks hooks;
    engine.step(0, hooks); // establish track

    ActionRequest engage{};
    engage.actor = 0;
    engage.type = ActionType::EngageTrack;
    engage.track_target = 10;
    engage.effect_profile_index = 0;

    engine.submit_action(engage);
    engine.step(1, hooks);

    ctx.check(!hooks.actions.empty(), "first engage resolves");
    ctx.check(hooks.actions.back().allowed, "first engage allowed");
    ctx.check(!hooks.effects.empty(), "first engage emits effect");
    ctx.check(hooks.effects.back().actor_ammo_before == 2 && hooks.effects.back().actor_ammo_after == 1,
              "engage spends one AP-equivalent ammo");

    engine.submit_action(engage);
    engine.step(2, hooks);
    ctx.check(!hooks.actions.back().allowed, "reaction trigger blocks engage during cooldown");
    ctx.check(has_reason(hooks.actions.back().failure_mask, GateFailureReason::Cooldown),
              "reaction trigger reports cooldown gate");
}

static void test_belief_publication_under_comm_constraints(TestContext& ctx) {
    Scenario scn = load_scenario("scenarios/mvp_comm_constraints.json");
    SimEngine engine;
    engine.init(scn);

    MsgProbeHooks hooks;
    for (int tick = 0; tick < scn.ticks; ++tick)
        engine.step(tick, hooks);

    bool sent_to_blue_tracker = false;
    bool sent_to_red_tracker = false;
    for (const auto& [sender, receiver] : hooks.delivered) {
        if (sender == 1 && receiver == 0)
            sent_to_blue_tracker = true;
        if (sender == 1 && receiver == 2)
            sent_to_red_tracker = true;
    }

    ctx.check(sent_to_blue_tracker, "belief publication delivered to same-team tracker");
    ctx.check(!sent_to_red_tracker, "belief publication blocked across team comm constraint");
}

static void test_deterministic_replay_checksums(TestContext& ctx) {
    Scenario scn = load_scenario("scenarios/mvp_contract_2v2.json");

    const std::string a_path = "tests/tmp_contract_a.replay";
    const std::string b_path = "tests/tmp_contract_b.replay";
    const std::string c_path = "tests/tmp_contract_c.replay";

    uint64_t checksum_a = run_replay_checksum(scn, a_path);
    uint64_t checksum_b = run_replay_checksum(scn, b_path);
    ctx.check(checksum_a == checksum_b, "same seed produces identical replay checksum");

    scn.seed += 1;
    uint64_t checksum_c = run_replay_checksum(scn, c_path);
    ctx.check(checksum_a != checksum_c, "different seed produces different replay checksum");

    std::remove(a_path.c_str());
    std::remove(b_path.c_str());
    std::remove(c_path.c_str());
}

} // namespace

int main() {
    return run_test_suite("contract", [](TestContext& ctx) {
        test_round_phase_order(ctx);
        test_ap_spending_and_reaction_trigger(ctx);
        test_belief_publication_under_comm_constraints(ctx);
        test_deterministic_replay_checksums(ctx);
    });
}
