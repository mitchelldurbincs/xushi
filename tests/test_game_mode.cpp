#include "test_helpers.h"
#include "../src/asset_protection_mode.h"
#include "../src/game_mode.h"
#include "../src/office_breach_mode.h"
#include "../src/scenario.h"
#include "../src/sim_engine.h"

#include <stdexcept>

static Scenario make_asset_scenario() {
    Scenario s;
    s.seed = 42;
    s.rounds = 5;
    s.ascii_map = {"WWWWW", "W...W", "W...W", "W...W", "WWWWW"};
    s.grid = GridMap::from_ascii(s.ascii_map);
    s.grid.recompute_rooms();

    s.game_mode.type = "asset_protection";
    s.game_mode.asset_entity_ids = {10, 11};

    ScenarioEntity a0;
    a0.id = 0; a0.kind = EntityKind::Operator;
    a0.pos = {1, 1}; a0.team = 0; a0.hp = 100; a0.max_hp = 100;
    s.entities.push_back(a0);
    ScenarioEntity a1;
    a1.id = 1; a1.kind = EntityKind::Operator;
    a1.pos = {3, 1}; a1.team = 1; a1.hp = 100; a1.max_hp = 100;
    s.entities.push_back(a1);
    ScenarioEntity asset0;
    asset0.id = 10; asset0.kind = EntityKind::Operator;
    asset0.pos = {1, 3}; asset0.team = 0; asset0.hp = 100; asset0.max_hp = 100;
    s.entities.push_back(asset0);
    ScenarioEntity asset1;
    asset1.id = 11; asset1.kind = EntityKind::Operator;
    asset1.pos = {3, 3}; asset1.team = 1; asset1.hp = 100; asset1.max_hp = 100;
    s.entities.push_back(asset1);
    return s;
}

static void test_factory_returns_nullptr_for_empty(TestContext& ctx) {
    Scenario s;
    s.game_mode.type = "";
    ctx.check(create_game_mode(s) == nullptr, "empty type -> nullptr");
}

static void test_factory_returns_asset_protection(TestContext& ctx) {
    Scenario s;
    s.game_mode.type = "asset_protection";
    auto m = create_game_mode(s);
    ctx.check(m != nullptr, "asset_protection constructed");
}

static void test_factory_returns_office_breach(TestContext& ctx) {
    Scenario s;
    s.game_mode.type = "office_breach";
    auto m = create_game_mode(s);
    ctx.check(m != nullptr, "office_breach constructed");
}

static void test_factory_throws_on_unknown(TestContext& ctx) {
    Scenario s;
    s.game_mode.type = "unknown";
    bool threw = false;
    try { create_game_mode(s); } catch (const std::runtime_error&) { threw = true; }
    ctx.check(threw, "unknown type throws");
}

static void test_asset_destroyed_triggers_win(TestContext& ctx) {
    Scenario s = make_asset_scenario();
    AssetProtectionMode mode;
    mode.init(s, s.entities);

    auto r1 = mode.on_round_end(0, s.entities);
    ctx.check(!r1.finished, "no asset destroyed -> not finished");

    mode.on_entity_damaged(1, 10, 100, 0);
    auto r2 = mode.on_round_end(1, s.entities);
    ctx.check(r2.finished, "asset 10 destroyed -> finished");
    ctx.check(r2.winning_team == 1, "team 1 wins when team 0 asset dies");
}

static void test_time_expiry_picks_healthier(TestContext& ctx) {
    Scenario s = make_asset_scenario();
    AssetProtectionMode mode;
    mode.init(s, s.entities);

    auto entities = s.entities;
    for (auto& e : entities)
        if (e.id == 10) e.hp = 50;

    auto r = mode.on_round_end(s.rounds - 1, entities);
    ctx.check(r.finished, "finished at last round");
    ctx.check(r.winning_team == 1, "healthier asset (team 1) wins");
}

static void test_office_breach_timeout_favors_defender(TestContext& ctx) {
    Scenario s;
    s.rounds = 3;
    s.ascii_map = {"WWW", "W.W", "WWW"};
    s.grid = GridMap::from_ascii(s.ascii_map);
    s.grid.recompute_rooms();
    ScenarioEntity atk; atk.id = 0; atk.kind = EntityKind::Operator;
    atk.pos = {1,1}; atk.team = 0; atk.hp = 100; atk.max_hp = 100;
    ScenarioEntity def; def.id = 1; def.kind = EntityKind::Operator;
    def.pos = {1,1}; def.team = 1; def.hp = 100; def.max_hp = 100;
    s.entities = {atk, def};

    OfficeBreachMode mode;
    mode.init(s, s.entities);
    auto r = mode.on_round_end(s.rounds - 1, s.entities);
    ctx.check(r.finished, "office_breach finishes at timeout");
    ctx.check(r.winning_team == 1, "defender wins on timeout");
}

static void test_office_breach_objective_triggers_attacker_win(TestContext& ctx) {
    Scenario s;
    s.rounds = 12;
    s.ascii_map = {"WWW", "W.W", "WWW"};
    s.grid = GridMap::from_ascii(s.ascii_map);
    s.grid.recompute_rooms();
    ScenarioEntity atk; atk.id = 0; atk.kind = EntityKind::Operator;
    atk.pos = {1,1}; atk.team = 0; atk.hp = 100; atk.max_hp = 100;
    ScenarioEntity def; def.id = 1; def.kind = EntityKind::Operator;
    def.pos = {1,1}; def.team = 1; def.hp = 100; def.max_hp = 100;
    s.entities = {atk, def};

    OfficeBreachMode mode;
    mode.init(s, s.entities);
    mode.notify_objective_interacted();
    auto r = mode.on_round_end(0, s.entities);
    ctx.check(r.finished, "game finishes when objective interacted");
    ctx.check(r.winning_team == 0, "attacker wins on objective");
}

int main() {
    return run_test_suite("game_mode", [](TestContext& ctx) {
        test_factory_returns_nullptr_for_empty(ctx);
        test_factory_returns_asset_protection(ctx);
        test_factory_returns_office_breach(ctx);
        test_factory_throws_on_unknown(ctx);
        test_asset_destroyed_triggers_win(ctx);
        test_time_expiry_picks_healthier(ctx);
        test_office_breach_timeout_favors_defender(ctx);
        test_office_breach_objective_triggers_attacker_win(ctx);
    });
}
