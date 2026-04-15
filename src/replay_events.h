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
