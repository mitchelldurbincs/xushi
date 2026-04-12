#include "sim_engine.h"
#include "constants.h"
#include "sensing.h"
#include "invariants.h"
#include <cmath>

// FNV-1a hash of entity positions and belief state.
uint64_t SimEngine::compute_world_hash() const {
    uint64_t h = 14695981039346656037ULL;
    auto mix = [&](const void* data, size_t len) {
        const auto* bytes = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < len; ++i) {
            h ^= bytes[i];
            h *= 1099511628211ULL;
        }
    };
    for (const auto& e : entities_) {
        mix(&e.id, sizeof(e.id));
        mix(&e.position.x, sizeof(float));
        mix(&e.position.y, sizeof(float));
        mix(&e.current_waypoint, sizeof(e.current_waypoint));
    }
    for (const auto& [owner_id, belief] : beliefs_) {
        mix(&owner_id, sizeof(owner_id));
        for (const auto& t : belief.tracks) {
            mix(&t.target, sizeof(t.target));
            mix(&t.estimated_position.x, sizeof(float));
            mix(&t.estimated_position.y, sizeof(float));
            mix(&t.confidence, sizeof(float));
            mix(&t.uncertainty, sizeof(float));
        }
    }
    return h;
}

void SimEngine::init(const Scenario& scn, Policy* policy) {
    scn_ = &scn;
    map_.obstacles = scn.obstacles;
    entities_ = scn.entities;

    sensors_.clear();
    trackers_.clear();
    observables_.clear();
    for (auto& e : entities_) {
        if (e.can_sense)     sensors_.push_back(&e);
        if (e.can_track)     trackers_.push_back(&e);
        if (e.is_observable) observables_.push_back(&e);
    }

    rng_ = Rng(scn.seed);
    comms_ = CommSystem{};
    beliefs_.clear();
    for (auto* t : trackers_)
        beliefs_[t->id] = BeliefState{};

    stats_ = SystemStats{};
    policy_ = policy ? policy : &null_policy_;
    active_tasks_.clear();
    tasks_assigned_ = 0;
    tasks_completed_ = 0;
}

void SimEngine::step(int tick, TickHooks& hooks) {
    const Scenario& scn = *scn_;

    // ── Movement ── task override > policy override > default
    for (auto& e : entities_) {
        auto task_it = active_tasks_.find(e.id);
        if (task_it != active_tasks_.end()) {
            Vec2 diff = task_it->second.target_position - e.position;
            float dist = diff.length();
            float step = e.speed * scn.dt;
            if (dist > kEpsilon && step < dist)
                e.position = e.position + diff * (step / dist);
            else if (dist > kEpsilon)
                e.position = task_it->second.target_position;
            hooks.on_entity_moved(tick, e.id, e.position);
            continue;
        }
        if (e.can_sense) {
            PolicyObservation pobs;
            pobs.id = e.id;
            pobs.position = e.position;
            pobs.tick = tick;
            auto belief_it = beliefs_.find(e.id);
            if (belief_it != beliefs_.end()) {
                auto& tracks = belief_it->second.tracks;
                for (const auto& t : tracks) {
                    if (pobs.num_tracks < kMaxPolicyTracks) {
                        pobs.local_tracks[pobs.num_tracks++] = t;
                    } else {
                        int worst = 0;
                        for (int j = 1; j < kMaxPolicyTracks; ++j)
                            if (pobs.local_tracks[j].confidence < pobs.local_tracks[worst].confidence)
                                worst = j;
                        if (t.confidence > pobs.local_tracks[worst].confidence)
                            pobs.local_tracks[worst] = t;
                    }
                }
            }
            {
                auto target = policy_->get_move_target(pobs);
                if (target) {
                    Vec2 diff = *target - e.position;
                    float dist = diff.length();
                    float step = e.speed * scn.dt;
                    if (dist > kEpsilon && step < dist)
                        e.position = e.position + diff * (step / dist);
                    else if (dist > kEpsilon)
                        e.position = *target;
                    hooks.on_entity_moved(tick, e.id, e.position);
                    continue;
                }
            }
        }
        auto event = update_movement(e, scn.dt, rng_);
        if (!e.waypoints.empty() || e.speed > 0.0f)
            hooks.on_entity_moved(tick, e.id, e.position);
        if (event.arrived)
            hooks.on_waypoint_arrival(tick, e.id, event.waypoint_index, e.position);
    }
    hooks.on_positions_check(entities_);

    // ── Sensing ──
    for (auto* sensor : sensors_) {
        std::vector<EntityId> detected_targets;
        for (auto* obs_ent : observables_) {
            if (sensor->id == obs_ent->id) continue;
            stats_.sensors_updated++;
            stats_.rays_cast++;

            Observation obs{};
            bool detected = sense(map_, sensor->position, sensor->id,
                                  obs_ent->position, obs_ent->id,
                                  scn.max_sensor_range, tick, rng_, obs,
                                  scn.perception.miss_rate);

            if (detected) {
                stats_.detections_generated++;
                detected_targets.push_back(obs_ent->id);

                if (sensor->can_track)
                    beliefs_[sensor->id].update(obs, tick);

                hooks.on_detection(tick, obs);

                MessagePayload payload;
                payload.type = MessagePayload::OBSERVATION;
                payload.observation = obs;

                for (auto* tracker : trackers_) {
                    float dist = (tracker->position - sensor->position).length();
                    int dt = comms_.send(sensor->id, tracker->id, payload, tick,
                                         dist, scn.channel, rng_);
                    if (dt >= 0) {
                        stats_.messages_sent++;
                        hooks.on_msg_sent(tick, sensor->id, tracker->id, dt);
                    } else {
                        stats_.messages_dropped++;
                        hooks.on_msg_dropped(tick, sensor->id, tracker->id);
                    }
                }
            } else {
                hooks.on_miss(tick, sensor->id);
            }
        }

        if (sensor->can_track) {
            beliefs_[sensor->id].apply_negative_evidence(
                sensor->position, scn.max_sensor_range, map_,
                detected_targets, scn.belief.negative_evidence_factor);
        }

        // False positive generation
        if (scn.perception.false_positive_rate > 0.0f &&
            rng_.uniform() < scn.perception.false_positive_rate) {
            float angle = rng_.uniform() * kTau;
            float range = rng_.uniform() * scn.max_sensor_range;
            Observation phantom{};
            phantom.tick = tick;
            phantom.observer = sensor->id;
            phantom.target = kPhantomTargetId;
            phantom.estimated_position = sensor->position +
                Vec2{std::cos(angle), std::sin(angle)} * range;
            phantom.uncertainty = kPhantomUncertainty;
            phantom.confidence = kPhantomConfidenceMin + rng_.uniform() * kPhantomConfidenceRange;
            phantom.is_false_positive = true;

            if (sensor->can_track)
                beliefs_[sensor->id].update(phantom, tick);

            hooks.on_detection(tick, phantom);
            hooks.on_false_positive(tick, sensor->id, phantom);

            MessagePayload payload;
            payload.type = MessagePayload::OBSERVATION;
            payload.observation = phantom;
            for (auto* tracker : trackers_) {
                float dist = (tracker->position - sensor->position).length();
                int dt = comms_.send(sensor->id, tracker->id, payload, tick,
                                     dist, scn.channel, rng_);
                if (dt >= 0) {
                    stats_.messages_sent++;
                    hooks.on_msg_sent(tick, sensor->id, tracker->id, dt);
                } else {
                    stats_.messages_dropped++;
                    hooks.on_msg_dropped(tick, sensor->id, tracker->id);
                }
            }
        }
    }

    // ── Communication ──
    std::vector<Message> delivered;
    comms_.deliver(tick, delivered);

    // ── Track expirations (snapshot before update) ──
    std::map<EntityId, std::vector<EntityId>> tracked_before;
    for (auto& [gid, belief] : beliefs_) {
        for (const auto& t : belief.tracks)
            tracked_before[gid].push_back(t.target);
    }

    // ── Belief ──
    for (const auto& msg : delivered) {
        stats_.messages_delivered++;
        auto it = beliefs_.find(msg.receiver);
        if (it != beliefs_.end())
            it->second.update(msg.payload.observation, tick);
    }
    for (auto& [gid, belief] : beliefs_)
        belief.decay(tick, scn.dt, scn.belief);

    // Invariant checks via hook
    for (auto& [gid, belief] : beliefs_)
        hooks.on_belief_invariant_check(belief);

    // Aggregate stats
    int total_active = 0;
    for (auto& [gid, belief] : beliefs_) {
        total_active += static_cast<int>(belief.tracks.size());
        for (EntityId id : tracked_before[gid]) {
            if (!belief.find_track(id))
                stats_.tracks_expired++;
        }
    }
    stats_.tracks_active = total_active;

    // Delivered message hooks
    for (const auto& msg : delivered)
        hooks.on_msg_delivered(tick, msg.sender, msg.receiver);

    // Track update/expired hooks
    for (auto& [gid, belief] : beliefs_) {
        for (const auto& t : belief.tracks)
            hooks.on_track_update(tick, gid, t);
        for (EntityId id : tracked_before[gid]) {
            if (!belief.find_track(id))
                hooks.on_track_expired(tick, gid, id);
        }
    }

    // ── Task completion ──
    std::vector<EntityId> completed_tasks;
    for (const auto& [eid, task] : active_tasks_) {
        ScenarioEntity* assigned = nullptr;
        for (auto& e : entities_) {
            if (e.id == eid) { assigned = &e; break; }
        }
        if (!assigned) continue;

        Vec2 diff = task.target_position - assigned->position;
        float dist_sq = diff.x * diff.x + diff.y * diff.y;
        if (dist_sq > kTaskArrivalRadius * kTaskArrivalRadius) continue;

        bool corroborated = false;
        for (auto& [gid, belief] : beliefs_) {
            const Track* trk = belief.find_track(task.target_id);
            if (trk && trk->status == TrackStatus::FRESH) {
                corroborated = true;
                break;
            }
        }
        completed_tasks.push_back(eid);
        tasks_completed_++;
        hooks.on_task_completed(tick, eid, task.target_id, corroborated);
    }
    for (EntityId eid : completed_tasks)
        active_tasks_.erase(eid);

    // ── Task assignment ── assign VERIFY for stale, low-confidence tracks
    for (auto* tracker_ent : trackers_) {
        auto& belief = beliefs_[tracker_ent->id];
        for (const auto& trk : belief.tracks) {
            if (trk.status != TrackStatus::STALE) continue;
            if (trk.confidence >= kTaskConfidenceThreshold) continue;

            bool already_tasked = false;
            for (const auto& [eid, t] : active_tasks_) {
                if (t.target_id == trk.target) { already_tasked = true; break; }
            }
            if (already_tasked) continue;

            ScenarioEntity* best = nullptr;
            float best_dist = kInfDistance;
            for (auto& e : entities_) {
                if (!e.can_sense) continue;
                if (active_tasks_.count(e.id)) continue;
                float d = (e.position - trk.estimated_position).length();
                if (d < best_dist) {
                    best = &e;
                    best_dist = d;
                }
            }

            if (best) {
                Task task;
                task.type = Task::Type::VERIFY;
                task.assigned_to = best->id;
                task.target_id = trk.target;
                task.target_position = trk.estimated_position;
                task.assigned_tick = tick;
                active_tasks_[best->id] = task;
                tasks_assigned_++;
                hooks.on_task_assigned(tick, task, *best);
            }
        }
    }

    // ── World hash every 10 ticks ──
    if (tick % 10 == 0) {
        uint64_t hash = compute_world_hash();
        hooks.on_world_hash(tick, hash);
        hooks.on_stats_snapshot(tick, stats_);
    }
}
