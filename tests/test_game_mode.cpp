#include "test_helpers.h"
#include "../src/sim_engine.h"
#include "../src/game_mode.h"
#include "../src/asset_protection_mode.h"
#include <stdexcept>

// Minimal scenario with two teams, each having a sensor/attacker and an asset.
static Scenario make_asset_protection_scenario() {
    Scenario s;
    s.seed = 42;
    s.dt = 1.0f;
    s.ticks = 50;
    s.max_sensor_range = 100.0f;

    s.game_mode_config.type = "asset_protection";
    s.game_mode_config.asset_entity_ids = {10, 11};

    // Team 0 attacker (sensor + tracker)
    ScenarioEntity atk0;
    atk0.id = 0;
    atk0.role_name = "attacker";
    atk0.team = 0;
    atk0.position = {10, 50};
    atk0.velocity = {0, 0};
    atk0.can_sense = true;
    atk0.can_track = true;
    atk0.is_observable = true;
    s.entities.push_back(atk0);

    // Team 1 attacker
    ScenarioEntity atk1;
    atk1.id = 1;
    atk1.role_name = "attacker";
    atk1.team = 1;
    atk1.position = {90, 50};
    atk1.velocity = {0, 0};
    atk1.can_sense = true;
    atk1.can_track = true;
    atk1.is_observable = true;
    s.entities.push_back(atk1);

    // Team 0 asset (base)
    ScenarioEntity asset0;
    asset0.id = 10;
    asset0.role_name = "base";
    asset0.team = 0;
    asset0.position = {5, 50};
    asset0.velocity = {0, 0};
    asset0.is_observable = true;
    asset0.vitality = 100;
    asset0.max_vitality = 100;
    s.entities.push_back(asset0);

    // Team 1 asset (base)
    ScenarioEntity asset1;
    asset1.id = 11;
    asset1.role_name = "base";
    asset1.team = 1;
    asset1.position = {95, 50};
    asset1.velocity = {0, 0};
    asset1.is_observable = true;
    asset1.vitality = 100;
    asset1.max_vitality = 100;
    s.entities.push_back(asset1);

    return s;
}

// Hook that records game mode end events.
struct GameModeHooks : TickHooks {
    std::vector<GameModeResult> results;
    void on_game_mode_end(int /*tick*/, const GameModeResult& r) override {
        results.push_back(r);
    }
};

// ── Factory tests ──

static void test_factory_returns_nullptr_when_no_mode(TestContext& ctx) {
    Scenario s;
    s.game_mode_config.type = "";
    auto mode = create_game_mode(s);
    ctx.check(mode == nullptr, "factory returns nullptr for empty type");
}

static void test_factory_returns_asset_protection(TestContext& ctx) {
    Scenario s;
    s.game_mode_config.type = "asset_protection";
    auto mode = create_game_mode(s);
    ctx.check(mode != nullptr, "factory returns non-null for asset_protection");
}

static void test_factory_throws_on_unknown_type(TestContext& ctx) {
    Scenario s;
    s.game_mode_config.type = "unknown_mode";
    bool threw = false;
    try {
        create_game_mode(s);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    ctx.check(threw, "factory throws on unknown game_mode type");
}

// ── Engine integration tests ──

static void test_no_game_mode_engine_works(TestContext& ctx) {
    Scenario scn = make_asset_protection_scenario();
    scn.game_mode_config.type = "";

    SimEngine engine;
    engine.init(scn);

    TickHooks hooks;
    for (int t = 0; t < 10; ++t)
        engine.step(t, hooks);

    ctx.check(!engine.has_game_mode(), "engine reports no game mode");
    ctx.check(!engine.game_mode_result().finished, "result is not finished");
}

// ── AssetProtection mode direct tests ──
// These test the game mode interface directly, bypassing the engagement
// system (which has a hardcoded stub gate that rejects all engagements).

static void test_asset_destroyed_triggers_win(TestContext& ctx) {
    Scenario scn = make_asset_protection_scenario();
    AssetProtectionMode mode;
    mode.init(scn, scn.entities);

    // Simulate damage to team 0's asset (entity 10): 100hp -> 50hp -> 0hp
    mode.on_entity_damaged(5, 10, 100, 50);

    // Not finished yet — asset still has health
    auto r1 = mode.on_tick_end(5, scn.entities);
    ctx.check(!r1.finished, "not finished after partial damage");

    // Kill the asset
    mode.on_entity_damaged(6, 10, 50, 0);

    // Now it should be finished
    auto r2 = mode.on_tick_end(6, scn.entities);
    ctx.check(r2.finished, "game finished after asset destroyed");
    ctx.check(r2.winning_team == 1, "team 1 wins when team 0 asset destroyed");
}

static void test_asset_destroyed_hook_fires(TestContext& ctx) {
    Scenario scn = make_asset_protection_scenario();
    auto mode = create_game_mode(scn);

    SimEngine engine;
    engine.init(scn, nullptr, mode.get());
    GameModeHooks hooks;

    // Run a tick so the engine is active
    engine.step(0, hooks);

    // Directly notify the game mode of lethal damage
    mode->on_entity_damaged(1, 10, 100, 0);

    // Next tick should trigger game end
    engine.step(1, hooks);

    ctx.check(engine.game_mode_result().finished, "game ended via engine");
    ctx.check(hooks.results.size() > 0, "game mode end hook fired");
    if (!hooks.results.empty()) {
        ctx.check(hooks.results[0].winning_team == 1,
              "hook reports team 1 wins");
    }
}

static void test_time_expiry_healthier_wins(TestContext& ctx) {
    Scenario scn = make_asset_protection_scenario();
    scn.ticks = 5;

    AssetProtectionMode mode;
    mode.init(scn, scn.entities);

    // Damage team 0's asset to 50hp (50%), team 1 asset stays at 100hp (100%)
    mode.on_entity_damaged(2, 10, 100, 50);

    // Simulate entity state with reduced vitality
    auto entities = scn.entities;
    for (auto& e : entities) {
        if (e.id == 10) e.vitality = 50;
    }

    // Not finished at tick 3 (last tick is 4)
    auto r1 = mode.on_tick_end(3, entities);
    ctx.check(!r1.finished, "not finished before last tick");

    // Finished at last tick (tick 4 = ticks-1)
    auto r2 = mode.on_tick_end(4, entities);
    ctx.check(r2.finished, "finished at time expiry");
    ctx.check(r2.winning_team == 1, "team 1 wins with healthier asset");
}

static void test_time_expiry_draw(TestContext& ctx) {
    Scenario scn = make_asset_protection_scenario();
    scn.ticks = 5;

    AssetProtectionMode mode;
    mode.init(scn, scn.entities);

    // No damage — both assets at full health
    auto r = mode.on_tick_end(4, scn.entities);
    ctx.check(r.finished, "finished at time expiry");
    ctx.check(r.winning_team == -1, "draw when assets have equal health");
}

static void test_second_asset_destroyed(TestContext& ctx) {
    Scenario scn = make_asset_protection_scenario();
    AssetProtectionMode mode;
    mode.init(scn, scn.entities);

    // Destroy team 1's asset (entity 11)
    mode.on_entity_damaged(3, 11, 100, 0);

    auto r = mode.on_tick_end(3, scn.entities);
    ctx.check(r.finished, "game finished when team 1 asset destroyed");
    ctx.check(r.winning_team == 0, "team 0 wins when team 1 asset destroyed");
}

// ── Validation tests ──

static void test_asset_init_fails_missing_entity(TestContext& ctx) {
    Scenario scn = make_asset_protection_scenario();
    scn.game_mode_config.asset_entity_ids.push_back(999);

    AssetProtectionMode mode;
    bool threw = false;
    try {
        mode.init(scn, scn.entities);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    ctx.check(threw, "init throws when asset entity not found");
}

static void test_asset_init_fails_no_team(TestContext& ctx) {
    Scenario scn = make_asset_protection_scenario();
    for (auto& e : scn.entities) {
        if (e.id == 10) e.team = -1;
    }

    AssetProtectionMode mode;
    bool threw = false;
    try {
        mode.init(scn, scn.entities);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    ctx.check(threw, "init throws when asset entity has no team");
}

static void test_team_field_default(TestContext& ctx) {
    ScenarioEntity e;
    ctx.check(e.team == -1, "default team is -1 (unaffiliated)");
}

static void test_non_asset_damage_ignored(TestContext& ctx) {
    Scenario scn = make_asset_protection_scenario();
    AssetProtectionMode mode;
    mode.init(scn, scn.entities);

    // Damage a non-asset entity (attacker, id=0)
    mode.on_entity_damaged(1, 0, 100, 0);

    // Should not end the game
    auto r = mode.on_tick_end(1, scn.entities);
    ctx.check(!r.finished, "non-asset damage does not end game");
}

int main() {
    return run_test_suite("game mode", [](TestContext& ctx) {
    test_factory_returns_nullptr_when_no_mode(ctx);
    test_factory_returns_asset_protection(ctx);
    test_factory_throws_on_unknown_type(ctx);
    test_no_game_mode_engine_works(ctx);
    test_asset_destroyed_triggers_win(ctx);
    test_asset_destroyed_hook_fires(ctx);
    test_time_expiry_healthier_wins(ctx);
    test_time_expiry_draw(ctx);
    test_second_asset_destroyed(ctx);
    test_non_asset_damage_ignored(ctx);
    test_asset_init_fails_missing_entity(ctx);
    test_asset_init_fails_no_team(ctx);
    test_team_field_default(ctx);
    });
}
