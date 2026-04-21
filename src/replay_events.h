#pragma once

#include "belief.h"
#include "game_mode.h"
#include "json.h"
#include "scenario.h"
#include "types.h"

#include <cstdint>
#include <cstdio>
#include <string>

// Replay event factories. Each returns a JsonValue ready to log via
// ReplayWriter. Adding a new event type = adding one function here.

inline JsonValue replay_header(const Scenario& scn, const std::string& scenario_path) {
    return json_object({
        {"type",     json_string("header")},
        {"scenario", json_string(scenario_path)},
        {"seed",     json_number(static_cast<double>(scn.seed))},
        {"rounds",   json_number(scn.rounds)},
        {"width",    json_number(scn.grid.width())},
        {"height",   json_number(scn.grid.height())},
    });
}

inline JsonValue replay_round_started(int round, int initiative_team) {
    return json_object({
        {"type",             json_string("round_started")},
        {"round",            json_number(round)},
        {"initiative_team",  json_number(initiative_team)},
    });
}

inline JsonValue replay_round_ended(int round) {
    return json_object({
        {"type",  json_string("round_ended")},
        {"round", json_number(round)},
    });
}

inline JsonValue replay_track_update(int round, int team, const Track& trk) {
    return json_object({
        {"type",     json_string("track_update")},
        {"round",    json_number(round)},
        {"team",     json_number(team)},
        {"target",   json_number(trk.target)},
        {"status",   json_string(track_status_str(trk.status))},
        {"pos",      json_array({json_number(trk.estimated_position.x),
                                  json_number(trk.estimated_position.y)})},
        {"conf",     json_number(trk.confidence)},
        {"unc",      json_number(trk.uncertainty)},
        {"class_id", json_number(trk.class_id)},
    });
}

inline JsonValue replay_track_expired(int round, int team, EntityId target) {
    return json_object({
        {"type",   json_string("track_expired")},
        {"round",  json_number(round)},
        {"team",   json_number(team)},
        {"target", json_number(target)},
    });
}

inline JsonValue replay_world_hash(int round, uint64_t hash) {
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx",
                  static_cast<unsigned long long>(hash));
    return json_object({
        {"type",  json_string("world_hash")},
        {"round", json_number(round)},
        {"hash",  json_string(buf)},
    });
}

inline JsonValue replay_game_mode_end(int round, const GameModeResult& r) {
    return json_object({
        {"type",          json_string("game_mode_end")},
        {"round",         json_number(round)},
        {"winning_team",  json_number(r.winning_team)},
        {"reason",        json_string(r.reason)},
    });
}

// Action / resolution events (contract §4, §7). Percent-points are integers
// per §14 (float values are inputs to compute_hit_probability only; anything
// that enters replay or world_hash stays integer).

inline JsonValue replay_unit_moved(int round, EntityId actor,
                                   GridPos from, GridPos to, int ap_after) {
    return json_object({
        {"type",     json_string("unit_moved")},
        {"round",    json_number(round)},
        {"actor",    json_number(actor)},
        {"from",     json_array({json_number(from.x), json_number(from.y)})},
        {"to",       json_array({json_number(to.x),   json_number(to.y)})},
        {"ap_after", json_number(ap_after)},
    });
}

struct ShotModifiers {
    int base_pct = 0;
    int fresh_delta = 0;
    int cover_delta = 0;
    int stale_delta = 0;
    int overwatch_delta = 0;
    int moved_delta = 0;
    int final_pct = 0;
    int rolled_pct = 0;
    bool hit = false;
};

inline JsonValue replay_shot_resolved(int round, EntityId shooter, EntityId target,
                                      const ShotModifiers& m) {
    return json_object({
        {"type",            json_string("shot_resolved")},
        {"round",           json_number(round)},
        {"shooter",         json_number(shooter)},
        {"target",          json_number(target)},
        {"base_pct",        json_number(m.base_pct)},
        {"fresh_delta",     json_number(m.fresh_delta)},
        {"cover_delta",     json_number(m.cover_delta)},
        {"stale_delta",     json_number(m.stale_delta)},
        {"overwatch_delta", json_number(m.overwatch_delta)},
        {"moved_delta",     json_number(m.moved_delta)},
        {"final_pct",       json_number(m.final_pct)},
        {"rolled_pct",      json_number(m.rolled_pct)},
        {"hit",             json_number(m.hit ? 1 : 0)},
    });
}

inline JsonValue replay_damage(int round, EntityId shooter, EntityId target,
                               int damage, int hp_after, bool eliminated) {
    return json_object({
        {"type",       json_string("damage")},
        {"round",      json_number(round)},
        {"shooter",    json_number(shooter)},
        {"target",     json_number(target)},
        {"damage",     json_number(damage)},
        {"hp_after",   json_number(hp_after)},
        {"eliminated", json_number(eliminated ? 1 : 0)},
    });
}

inline JsonValue replay_overwatch_set(int round, EntityId actor) {
    return json_object({
        {"type",  json_string("overwatch_set")},
        {"round", json_number(round)},
        {"actor", json_number(actor)},
    });
}

inline JsonValue replay_door_state(int round, GridPos a, GridPos b,
                                   DoorState new_state, const char* cause) {
    return json_object({
        {"type",      json_string("door_state")},
        {"round",     json_number(round)},
        {"a",         json_array({json_number(a.x), json_number(a.y)})},
        {"b",         json_array({json_number(b.x), json_number(b.y)})},
        {"new_state", json_string(door_state_str(new_state))},
        {"cause",     json_string(cause)},
    });
}
