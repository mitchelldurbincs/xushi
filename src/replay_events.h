#pragma once

#include "json.h"
#include "types.h"
#include "sensing.h"
#include "belief.h"
#include "scenario.h"
#include "stats.h"
#include <cstdint>

// Event factory helpers. Each returns a JsonValue ready to log.
// Adding a new event type = adding one function here. The replay
// writer/reader never change.

inline JsonValue replay_header(const Scenario& scn, const std::string& scenario_path) {
    return json_object({
        {"type",     json_string("header")},
        {"scenario", json_string(scenario_path)},
        {"seed",     json_number(static_cast<double>(scn.seed))},
        {"dt",       json_number(scn.dt)},
        {"ticks",    json_number(scn.ticks)},
    });
}

inline JsonValue replay_detection(int tick, const Observation& obs) {
    return json_object({
        {"type",     json_string("detection")},
        {"tick",     json_number(tick)},
        {"observer", json_number(obs.observer)},
        {"target",   json_number(obs.target)},
        {"est_pos",  json_array({json_number(obs.estimated_position.x),
                                  json_number(obs.estimated_position.y)})},
        {"conf",     json_number(obs.confidence)},
        {"unc",      json_number(obs.uncertainty)},
    });
}

inline JsonValue replay_msg_sent(int tick, EntityId sender, EntityId receiver, int delivery_tick) {
    return json_object({
        {"type",          json_string("msg_sent")},
        {"tick",          json_number(tick)},
        {"sender",        json_number(sender)},
        {"receiver",      json_number(receiver)},
        {"delivery_tick", json_number(delivery_tick)},
    });
}

inline JsonValue replay_msg_delivered(int tick, EntityId sender, EntityId receiver) {
    return json_object({
        {"type",     json_string("msg_delivered")},
        {"tick",     json_number(tick)},
        {"sender",   json_number(sender)},
        {"receiver", json_number(receiver)},
    });
}

inline JsonValue replay_msg_dropped(int tick, EntityId sender, EntityId receiver) {
    return json_object({
        {"type",     json_string("msg_dropped")},
        {"tick",     json_number(tick)},
        {"sender",   json_number(sender)},
        {"receiver", json_number(receiver)},
    });
}

inline JsonValue replay_track_update(int tick, EntityId owner, const Track& trk) {
    return json_object({
        {"type",   json_string("track_update")},
        {"tick",   json_number(tick)},
        {"owner",  json_number(owner)},
        {"target", json_number(trk.target)},
        {"status", json_string(track_status_str(trk.status))},
        {"pos",    json_array({json_number(trk.estimated_position.x),
                                json_number(trk.estimated_position.y)})},
        {"conf",   json_number(trk.confidence)},
        {"unc",    json_number(trk.uncertainty)},
    });
}

inline JsonValue replay_track_expired(int tick, EntityId owner, EntityId target) {
    return json_object({
        {"type",   json_string("track_expired")},
        {"tick",   json_number(tick)},
        {"owner",  json_number(owner)},
        {"target", json_number(target)},
    });
}

inline JsonValue replay_stats(int tick, const SystemStats& s) {
    return json_object({
        {"type",               json_string("stats")},
        {"tick",               json_number(tick)},
        {"rays_cast",          json_number(s.rays_cast)},
        {"detections",         json_number(s.detections_generated)},
        {"messages_sent",      json_number(s.messages_sent)},
        {"messages_dropped",   json_number(s.messages_dropped)},
        {"messages_delivered",  json_number(s.messages_delivered)},
        {"tracks_active",      json_number(s.tracks_active)},
        {"tracks_expired",     json_number(s.tracks_expired)},
    });
}

inline JsonValue replay_entity_position(int tick, EntityId id, Vec2 pos) {
    return json_object({
        {"type",   json_string("entity_pos")},
        {"tick",   json_number(tick)},
        {"entity", json_number(id)},
        {"pos",    json_array({json_number(pos.x), json_number(pos.y)})},
    });
}

inline JsonValue replay_waypoint_arrival(int tick, EntityId id, int waypoint_index, Vec2 pos) {
    return json_object({
        {"type",     json_string("waypoint_arrival")},
        {"tick",     json_number(tick)},
        {"entity",   json_number(id)},
        {"waypoint", json_number(waypoint_index)},
        {"pos",      json_array({json_number(pos.x), json_number(pos.y)})},
    });
}

inline JsonValue replay_world_hash(int tick, uint64_t hash) {
    // Emit hash as hex string for readability
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(hash));
    return json_object({
        {"type", json_string("world_hash")},
        {"tick", json_number(tick)},
        {"hash", json_string(buf)},
    });
}
