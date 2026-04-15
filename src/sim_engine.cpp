#include "sim_engine.h"
#include "constants.h"
#include "sensing.h"
#include "invariants.h"
#include "engagement.h"
#include "world_hash.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <set>
#include <stdexcept>
#include <unordered_set>

size_t SimEngine::GridCoordHash::operator()(const SimEngine::GridCoord& c) const {
    const uint64_t ux = static_cast<uint32_t>(c.x);
    const uint64_t uy = static_cast<uint32_t>(c.y);
    return static_cast<size_t>((ux << 32) ^ uy);
}

// FNV-1a hash of entity positions and belief state.
uint64_t SimEngine::compute_world_hash() const {
    return compute_world_hash_canonical(entities_, beliefs_.states());
}

void SimEngine::init(const Scenario& scn, Policy* policy, GameMode* game_mode) {
    scn_ = &scn;
    map_.obstacles = scn.obstacles;
    entities_ = scn.entities;

    sensors_.clear();
    trackers_.clear();
    trackers_no_team_.clear();
    trackers_by_team_.clear();
    observables_.clear();
    for (auto& e : entities_) {
        if (e.can_sense)     sensors_.push_back(&e);
        if (e.can_track) {
            trackers_.push_back(&e);
            if (e.team < 0) trackers_no_team_.push_back(&e);
            else trackers_by_team_[e.team].push_back(&e);
        }
        if (e.is_observable) observables_.push_back(&e);
    }
    rebuild_entity_index();
    spatial_cell_size_ = std::max(kEpsilon, scn.max_sensor_range);
    rebuild_spatial_bins(spatial_cell_size_);

    rng_ = Rng(scn.seed);
    comms_ = CommSystem{};
    beliefs_.init_trackers(trackers_);
    truth_state_.clear();

    stats_ = SystemStats{};
    policy_ = policy ? policy : &null_policy_;
    active_tasks_.clear();
    tasks_assigned_ = 0;
    tasks_completed_ = 0;

    pending_actions_.clear();
    designations_.clear();
    next_designation_id_ = 1;

    game_mode_ = game_mode;
    last_game_mode_result_ = GameModeResult{};
    if (game_mode_)
        game_mode_->init(scn, entities_);

    round_state_ = RoundState{};
    round_context_ = RoundContext{};
    next_round_number_ = 0;
}

void SimEngine::require_phase(RoundPhase expected) const {
    if (round_state_.phase != expected)
        throw std::logic_error("illegal round phase transition");
}

void SimEngine::advance_phase(RoundPhase next_phase) {
    round_state_.phase = next_phase;
}

bool SimEngine::is_support_action(ActionType type) const {
    return type == ActionType::DesignateTrack ||
           type == ActionType::ClearDesignation ||
           type == ActionType::RequestBDA;
}

bool SimEngine::is_operator_action(ActionType type) const {
    return type == ActionType::EngageTrack;
}

bool SimEngine::is_action_type_allowed_in_phase(ActionType type, RoundPhase phase) const {
    if (is_support_action(type))
        return phase == RoundPhase::SupportPublicationGate;
    if (is_operator_action(type))
        return phase == RoundPhase::Activations;
    return false;
}

void SimEngine::build_activation_order_for_round() {
    round_context_.activation_order.clear();

    std::vector<size_t> same_team;
    std::vector<size_t> other_teams;
    same_team.reserve(entities_.size());
    other_teams.reserve(entities_.size());

    for (size_t i = 0; i < entities_.size(); ++i) {
        const auto& e = entities_[i];
        if (e.vitality <= 0)
            continue;
        if (e.team == round_context_.initiative_team) {
            same_team.push_back(i);
        } else {
            other_teams.push_back(i);
        }
    }

    round_context_.activation_order.insert(round_context_.activation_order.end(),
                                           same_team.begin(), same_team.end());
    round_context_.activation_order.insert(round_context_.activation_order.end(),
                                           other_teams.begin(), other_teams.end());
}

void SimEngine::reset_round_context(int tick) {
    const int next_round = next_round_number_++;
    round_context_ = RoundContext{};
    round_context_.round_number = next_round;
    round_state_.round_number = next_round;
    round_state_.round_tick = tick;

    std::set<int> active_teams;
    for (const auto& e : entities_) {
        if (e.team >= 0)
            active_teams.insert(e.team);
    }

    if (!active_teams.empty()) {
        const size_t owner_index = static_cast<size_t>(next_round) % active_teams.size();
        auto team_it = active_teams.begin();
        std::advance(team_it, owner_index);
        round_context_.initiative_team = *team_it;
        round_state_.initiative_team = *team_it;
    } else {
        round_context_.initiative_team = -1;
        round_state_.initiative_team = -1;
    }

    for (const auto& e : entities_) {
        if (e.vitality <= 0)
            continue;
        constexpr int kDefaultOperatorApMax = 1;
        round_context_.operator_ap_max[e.id] = kDefaultOperatorApMax;
        round_context_.operator_ap[e.id] = kDefaultOperatorApMax;
        if (e.team >= 0 && !round_context_.support_ap_max.count(e.team)) {
            constexpr int kDefaultSupportApMax = 1;
            round_context_.support_ap_max[e.team] = kDefaultSupportApMax;
            round_context_.support_ap[e.team] = kDefaultSupportApMax;
            round_context_.support_ap_spent[e.team] = 0;
        }
    }

    build_activation_order_for_round();
    round_state_.activation_index = 0;
    round_context_.activation_cursor = 0;
}

void SimEngine::begin_round(int tick, TickHooks& hooks) {
    require_phase(RoundPhase::Idle);
    reset_round_context(tick);
    advance_phase(RoundPhase::RoundStart);

    if (game_mode_)
        game_mode_->on_tick_start(tick, entities_);

    const auto run_phase = [&](const char* phase, auto&& fn) {
        const auto t0 = std::chrono::steady_clock::now();
        fn();
        const auto t1 = std::chrono::steady_clock::now();
        const std::chrono::duration<double, std::micro> elapsed = t1 - t0;
        hooks.on_phase_timing(phase, elapsed.count());
    };

    advance_phase(RoundPhase::Cooldowns);
    run_phase("cooldowns", [&] { tick_cooldowns(); });

    advance_phase(RoundPhase::Activations);
}

bool SimEngine::step_activation(int tick, TickHooks& hooks) {
    if (tick != round_state_.round_tick)
        throw std::logic_error("activation tick mismatch");

    require_phase(RoundPhase::Activations);

    if (round_context_.activation_cursor >= round_context_.activation_order.size()) {
        hooks.on_positions_check(entities_);
        return true;
    }

    const auto run_phase = [&](const char* phase, auto&& fn) {
        const auto t0 = std::chrono::steady_clock::now();
        fn();
        const auto t1 = std::chrono::steady_clock::now();
        const std::chrono::duration<double, std::micro> elapsed = t1 - t0;
        hooks.on_phase_timing(phase, elapsed.count());
    };

    run_phase("activation", [&] {
        while (round_context_.activation_cursor < round_context_.activation_order.size()) {
            const size_t entity_index = round_context_.activation_order[round_context_.activation_cursor];
            ++round_context_.activation_cursor;
            round_state_.activation_index = round_context_.activation_cursor;
            if (entity_index >= entities_.size())
                continue;
            if (entities_[entity_index].vitality <= 0)
                continue;
            tick_activation(tick, entity_index, hooks);
            break;
        }
    });
    if (round_context_.activation_cursor >= round_context_.activation_order.size()) {
        hooks.on_positions_check(entities_);
        return true;
    }
    return false;
}

void SimEngine::finalize_round(int tick, TickHooks& hooks) {
    if (tick != round_state_.round_tick)
        throw std::logic_error("round finalize tick mismatch");

    require_phase(RoundPhase::Activations);
    if (round_context_.activation_cursor < round_context_.activation_order.size())
        throw std::logic_error("cannot finalize round before all activations");

    const auto run_phase = [&](const char* phase, auto&& fn) {
        const auto t0 = std::chrono::steady_clock::now();
        fn();
        const auto t1 = std::chrono::steady_clock::now();
        const std::chrono::duration<double, std::micro> elapsed = t1 - t0;
        hooks.on_phase_timing(phase, elapsed.count());
    };

    // Spatial bins are referenced by tasking and other spatial queries below;
    // rebuild once after all activations have updated positions.
    rebuild_spatial_bins(spatial_cell_size_);

    advance_phase(RoundPhase::SupportPublicationGate);
    run_phase("support_publication_gate", [&] { tick_support_publication_gate(tick, hooks); });

    std::vector<Message> delivered;
    advance_phase(RoundPhase::Communication);
    run_phase("communication", [&] { tick_communication(tick, hooks, delivered); });

    advance_phase(RoundPhase::Belief);
    run_phase("belief", [&] { tick_belief(tick, hooks, delivered); });

    advance_phase(RoundPhase::ReactionResolution);
    run_phase("reaction_resolution", [&] { tick_reaction_resolution(tick, hooks); });

    advance_phase(RoundPhase::Tasks);
    run_phase("tasks", [&] { tick_tasks(tick, hooks); });

    advance_phase(RoundPhase::PeriodicSnapshots);
    run_phase("periodic_snapshots", [&] { tick_periodic_snapshots(tick, hooks); });

    advance_phase(RoundPhase::RoundEnd);
    if (game_mode_) {
        last_game_mode_result_ = game_mode_->on_tick_end(tick, entities_);
        if (last_game_mode_result_.finished)
            hooks.on_game_mode_end(tick, last_game_mode_result_);
    }

    round_state_ = RoundState{};
    round_context_ = RoundContext{};
}

void SimEngine::step(int tick, TickHooks& hooks) {
    begin_round(tick, hooks);
    while (!step_activation(tick, hooks)) {
    }
    finalize_round(tick, hooks);
}

void SimEngine::tick_cooldowns() {
    for (auto& e : entities_) {
        if (e.cooldown_ticks_remaining > 0)
            --e.cooldown_ticks_remaining;
    }
}

void SimEngine::move_toward_target(ScenarioEntity& entity, const Vec2& target) const {
    const Scenario& scn = *scn_;
    const Vec2 diff = target - entity.position;
    const float dist = diff.length();
    const float step = entity.speed * scn.dt;
    if (dist > kEpsilon && step < dist)
        entity.position = entity.position + diff * (step / dist);
    else if (dist > kEpsilon)
        entity.position = target;
}

void SimEngine::tick_activation(int tick, size_t activation_index, TickHooks& hooks) {
    const Scenario& scn = *scn_;
    ScenarioEntity& e = entities_[activation_index];
    if (e.vitality <= 0)
        return;

    bool hold_position_for_engagement = false;
    if (e.can_engage && !e.allowed_effect_profile_indices.empty()) {
        int ep_idx = e.allowed_effect_profile_indices[0];
        float engage_range = 0.0f;
        if (ep_idx >= 0 && ep_idx < static_cast<int>(scn.effect_profiles.size()))
            engage_range = scn.effect_profiles[ep_idx].range;
        if (engage_range > 0.0f) {
            for (const auto& other : entities_) {
                if (other.id == e.id) continue;
                if (!other.can_engage || other.vitality <= 0) continue;
                if (e.team >= 0 && other.team >= 0 && e.team == other.team) continue;
                float dist = (other.position - e.position).length();
                if (dist <= engage_range) {
                    hold_position_for_engagement = true;
                    break;
                }
            }
        }
    }

    if (hold_position_for_engagement) {
        hooks.on_entity_moved(tick, e.id, e.position);
    } else if (active_tasks_.count(e.id)) {
        const Task& task = active_tasks_[e.id];
        move_toward_target(e, task.target_position);
        hooks.on_entity_moved(tick, e.id, e.position);
    } else if (e.can_sense) {
        PolicyObservation pobs;
        pobs.id = e.id;
        pobs.position = e.position;
        pobs.tick = tick;
        const BeliefState* belief = beliefs_.find(e.id);
        if (belief) {
            auto& tracks = belief->tracks;
            for (const auto& t : tracks) {
                PolicyTrackObservation policy_track{};
                policy_track.target = t.target;
                policy_track.estimated_position = t.estimated_position;
                policy_track.confidence = t.confidence;
                policy_track.uncertainty = t.uncertainty;
                policy_track.status = t.status;
                policy_track.class_id = t.class_id;
                policy_track.identity_confidence = t.identity_confidence;

                if (pobs.num_tracks < kMaxPolicyTracks) {
                    pobs.local_tracks[pobs.num_tracks++] = policy_track;
                } else {
                    int worst = 0;
                    for (int j = 1; j < kMaxPolicyTracks; ++j)
                        if (pobs.local_tracks[j].confidence < pobs.local_tracks[worst].confidence)
                            worst = j;
                    if (policy_track.confidence > pobs.local_tracks[worst].confidence)
                        pobs.local_tracks[worst] = policy_track;
                }
            }
        }

        auto target = policy_->get_move_target(pobs);
        if (target) {
            move_toward_target(e, *target);
            hooks.on_entity_moved(tick, e.id, e.position);
        } else {
            auto event = update_movement(e, scn.dt, rng_);
            if (!e.waypoints.empty() || e.speed > 0.0f)
                hooks.on_entity_moved(tick, e.id, e.position);
            if (event.arrived)
                hooks.on_waypoint_arrival(tick, e.id, event.waypoint_index, e.position);
        }
    } else {
        auto event = update_movement(e, scn.dt, rng_);
        if (!e.waypoints.empty() || e.speed > 0.0f)
            hooks.on_entity_moved(tick, e.id, e.position);
        if (event.arrived)
            hooks.on_waypoint_arrival(tick, e.id, event.waypoint_index, e.position);
    }

    if (!e.can_sense)
        return;

    std::vector<EntityId> detected_targets;
    for (auto* obs_ent : observables_) {
        if (e.id == obs_ent->id) continue;
        stats_.sensors_updated++;
        stats_.rays_cast++;

        Observation obs{};
        bool detected = sense(map_, e.position, e.id,
                              obs_ent->position, obs_ent->id,
                              scn.max_sensor_range, tick, rng_, obs,
                              scn.perception.miss_rate,
                              obs_ent->class_id,
                              scn.perception.class_confusion_rate);
        if (detected) {
            stats_.detections_generated++;
            detected_targets.push_back(obs_ent->id);
            if (e.can_track)
                beliefs_.apply_local_observation(e.id, obs, tick);
            hooks.on_detection(tick, obs);

            truth_state_.queue_publication(e.id, obs);
        } else {
            hooks.on_miss(tick, e.id);
        }
    }

    if (e.can_track) {
        beliefs_.apply_negative_evidence(e.id,
            e.position, scn.max_sensor_range, map_,
            detected_targets, scn.belief.negative_evidence_factor);
    }

    if (scn.perception.false_positive_rate > 0.0f &&
        rng_.uniform() < scn.perception.false_positive_rate) {
        float angle = rng_.uniform() * kTau;
        float range = rng_.uniform() * scn.max_sensor_range;
        Observation phantom{};
        phantom.tick = tick;
        phantom.observer = e.id;
        phantom.target = kPhantomTargetId;
        phantom.estimated_position = e.position +
            Vec2{std::cos(angle), std::sin(angle)} * range;
        phantom.uncertainty = kPhantomUncertainty;
        phantom.confidence = kPhantomConfidenceMin + rng_.uniform() * kPhantomConfidenceRange;
        phantom.is_false_positive = true;

        if (e.can_track)
            beliefs_.apply_local_observation(e.id, phantom, tick);

        hooks.on_detection(tick, phantom);
        hooks.on_false_positive(tick, e.id, phantom);

        truth_state_.queue_publication(e.id, phantom);
    }
}

void SimEngine::tick_communication(int tick, TickHooks& hooks, std::vector<Message>& delivered) {
    const Scenario& scn = *scn_;
    for (const auto& publication : truth_state_.pending_publications()) {
        const ScenarioEntity* sender = nullptr;
        for (const auto& entity : entities_) {
            if (entity.id == publication.sender) {
                sender = &entity;
                break;
            }
        }
        if (!sender)
            continue;

        MessagePayload payload;
        payload.type = MessagePayload::OBSERVATION;
        payload.observation = publication.payload;
        for (auto* tracker : trackers_) {
            if (sender->team >= 0 && tracker->team >= 0 && sender->team != tracker->team)
                continue;
            float dist = (tracker->position - sender->position).length();
            int dt = comms_.send(sender->id, tracker->id, payload, tick, dist, scn.channel, rng_);
            if (dt >= 0) {
                stats_.messages_sent++;
                hooks.on_msg_sent(tick, sender->id, tracker->id, dt);
            } else {
                stats_.messages_dropped++;
                hooks.on_msg_dropped(tick, sender->id, tracker->id);
            }
        }
    }
    truth_state_.clear();

    comms_.deliver(tick, delivered);
}

void SimEngine::tick_belief(int tick, TickHooks& hooks, const std::vector<Message>& delivered) {
    const Scenario& scn = *scn_;
    std::map<EntityId, std::vector<EntityId>> tracked_before;
    for (auto& [gid, belief] : beliefs_.states()) {
        for (const auto& t : belief.tracks)
            tracked_before[gid].push_back(t.target);
    }

    for (const auto& msg : delivered) {
        stats_.messages_delivered++;
        beliefs_.apply_published_observation(msg.receiver, msg.payload.observation, tick);
    }
    for (auto& [gid, belief] : beliefs_.states())
        belief.decay(tick, scn.dt, scn.belief);

    for (auto& [gid, belief] : beliefs_.states())
        hooks.on_belief_invariant_check(belief);

    int total_active = 0;
    for (auto& [gid, belief] : beliefs_.states()) {
        total_active += static_cast<int>(belief.tracks.size());
        for (EntityId id : tracked_before[gid]) {
            if (!belief.find_track(id))
                stats_.tracks_expired++;
        }
    }
    stats_.tracks_active = total_active;

    for (const auto& msg : delivered)
        hooks.on_msg_delivered(tick, msg.sender, msg.receiver);

    for (auto& [gid, belief] : beliefs_.states()) {
        for (const auto& t : belief.tracks)
            hooks.on_track_update(tick, gid, t);
        for (EntityId id : tracked_before[gid]) {
            if (!belief.find_track(id))
                hooks.on_track_expired(tick, gid, id);
        }
    }
}

void SimEngine::tick_tasks(int tick, TickHooks& hooks) {
    const Scenario& scn = *scn_;
    std::vector<EntityId> completed_tasks;
    for (const auto& [eid, task] : active_tasks_) {
        ScenarioEntity* assigned = find_entity(eid);
        if (!assigned) continue;

        Vec2 diff = task.target_position - assigned->position;
        float dist_sq = diff.x * diff.x + diff.y * diff.y;
        if (dist_sq > kTaskArrivalRadius * kTaskArrivalRadius) continue;

        bool corroborated = false;
        for (auto& [gid, belief] : beliefs_.states()) {
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

    for (auto* tracker_ent : trackers_) {
        BeliefState* belief = beliefs_.find(tracker_ent->id);
        if (!belief) continue;
        for (const auto& trk : belief->tracks) {
            if (trk.status != TrackStatus::STALE) continue;
            if (trk.confidence >= kTaskConfidenceThreshold) continue;

            bool already_tasked = false;
            for (const auto& [eid, t] : active_tasks_) {
                if (t.target_id == trk.target) { already_tasked = true; break; }
            }
            if (already_tasked) continue;

            ScenarioEntity* best = nullptr;
            float best_dist = kInfDistance;
            const auto try_candidates = [&](float range) {
                for_each_candidate_in_range(trk.estimated_position, range, [&](size_t idx) {
                    auto& e = entities_[idx];
                    if (!e.can_sense) return;
                    if (active_tasks_.count(e.id)) return;
                    float d = (e.position - trk.estimated_position).length();
                    if (d < best_dist) {
                        best = &e;
                        best_dist = d;
                    }
                });
            };
            try_candidates(scn.max_sensor_range);
            if (!best)
                try_candidates(kInfDistance);

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
}

void SimEngine::tick_support_publication_gate(int tick, TickHooks& hooks) {
    adjudicate_actions_for_type(tick, hooks, ActionType::DesignateTrack);
    adjudicate_actions_for_type(tick, hooks, ActionType::ClearDesignation);
    adjudicate_actions_for_type(tick, hooks, ActionType::RequestBDA);
}

void SimEngine::tick_reaction_resolution(int tick, TickHooks& hooks) {
    adjudicate_actions_for_type(tick, hooks, ActionType::EngageTrack);

    // Expire stale designations at end of reaction resolution.
    designations_.erase(
        std::remove_if(designations_.begin(), designations_.end(),
            [tick](const DesignationRecord& d) { return tick >= d.expires_tick; }),
        designations_.end());

    pending_actions_.clear();
}

void SimEngine::tick_periodic_snapshots(int tick, TickHooks& hooks) {
    if (tick % 10 == 0) {
        uint64_t hash = compute_world_hash();
        hooks.on_world_hash(tick, hash);
        hooks.on_stats_snapshot(tick, stats_);
    }
}

void SimEngine::submit_action(const ActionRequest& req) {
    const RoundPhase phase = round_state_.phase;
    const bool pre_round_queue = (phase == RoundPhase::Idle || phase == RoundPhase::RoundStart);
    if (!pre_round_queue && !is_action_type_allowed_in_phase(req.type, phase))
        throw std::logic_error("action submitted in illegal round phase");
    pending_actions_.push_back(req);
}

ScenarioEntity* SimEngine::find_entity(EntityId id) {
    auto it = entity_index_.find(id);
    return it == entity_index_.end() ? nullptr : &entities_[it->second];
}

const ScenarioEntity* SimEngine::find_entity(EntityId id) const {
    auto it = entity_index_.find(id);
    return it == entity_index_.end() ? nullptr : &entities_[it->second];
}

void SimEngine::rebuild_entity_index() {
    entity_index_.clear();
    entity_index_.reserve(entities_.size());
    for (size_t i = 0; i < entities_.size(); ++i)
        entity_index_[entities_[i].id] = i;
}

void SimEngine::rebuild_spatial_bins(float cell_size) {
    spatial_bins_.clear();
    if (cell_size <= kEpsilon) return;
    for (size_t i = 0; i < entities_.size(); ++i) {
        const Vec2 p = entities_[i].position;
        const int gx = static_cast<int>(std::floor(p.x / cell_size));
        const int gy = static_cast<int>(std::floor(p.y / cell_size));
        spatial_bins_[GridCoord{gx, gy}].push_back(i);
    }
}

void SimEngine::for_each_candidate_in_range(const Vec2& center, float range,
                                            const std::function<void(size_t)>& fn) const {
    // Fallback: empty bins, invalid cell size, non-finite range, or very large range
    if (spatial_bins_.empty() || spatial_cell_size_ <= kEpsilon || !std::isfinite(range) || range >= kInfDistance / 2) {
        for (size_t i = 0; i < entities_.size(); ++i) fn(i);
        return;
    }
    const int cx = static_cast<int>(std::floor(center.x / spatial_cell_size_));
    const int cy = static_cast<int>(std::floor(center.y / spatial_cell_size_));
    const int radius_cells = static_cast<int>(std::ceil(range / spatial_cell_size_));
    for (int dx = -radius_cells; dx <= radius_cells; ++dx) {
        for (int dy = -radius_cells; dy <= radius_cells; ++dy) {
            auto it = spatial_bins_.find(GridCoord{cx + dx, cy + dy});
            if (it == spatial_bins_.end()) continue;
            for (size_t idx : it->second)
                fn(idx);
        }
    }
}

const Scenario::EffectProfile* SimEngine::find_effect_profile(uint32_t index) const {
    if (!scn_) return nullptr;
    if (index >= scn_->effect_profiles.size()) return nullptr;
    return &scn_->effect_profiles[index];
}

void SimEngine::adjudicate_actions_for_type(int tick, TickHooks& hooks, ActionType type) {
    for (const auto& req : pending_actions_) {
        if (req.type != type)
            continue;

        ActionResult result;
        result.request = req;
        result.tick = tick;

        // Look up actor's belief state
        auto belief_it = beliefs_.states().find(req.actor);

        switch (req.type) {
        case ActionType::DesignateTrack: {
            // Check that actor has a belief and the track exists
            if (belief_it == beliefs_.states().end() ||
                !belief_it->second.find_track(req.track_target)) {
                result.allowed = false;
                result.failure_mask = static_cast<uint32_t>(GateFailureReason::TrackNotFound);
            } else if (ScenarioEntity* actor = find_entity(req.actor); !actor) {
                result.allowed = false;
                result.failure_mask = static_cast<uint32_t>(GateFailureReason::NoCapability);
            } else if (actor->team >= 0 && round_context_.support_ap[actor->team] <= 0) {
                result.allowed = false;
                result.failure_mask = static_cast<uint32_t>(GateFailureReason::OutOfAmmo);
            } else {
                result.allowed = true;
                DesignationRecord rec;
                rec.designation_id = next_designation_id_++;
                rec.issuer = req.actor;
                rec.track_target = req.track_target;
                rec.kind = req.desig_kind;
                rec.priority = req.priority;
                rec.created_tick = tick;
                rec.expires_tick = tick + kDefaultDesignationTTL;
                designations_.push_back(rec);
                if (actor->team >= 0) {
                    round_context_.support_ap[actor->team] -= 1;
                    round_context_.support_ap_spent[actor->team] += 1;
                }
            }
            break;
        }
        case ActionType::ClearDesignation: {
            bool found = false;
            ScenarioEntity* actor = find_entity(req.actor);
            for (auto it = designations_.begin(); it != designations_.end(); ++it) {
                if (it->track_target == req.track_target &&
                    it->issuer == req.actor &&
                    it->kind == req.desig_kind) {
                    designations_.erase(it);
                    found = true;
                    break;
                }
            }
            result.allowed = found && actor &&
                (actor->team < 0 || round_context_.support_ap[actor->team] > 0);
            if (!found)
                result.failure_mask = static_cast<uint32_t>(GateFailureReason::TrackNotFound);
            else if (!actor)
                result.failure_mask = static_cast<uint32_t>(GateFailureReason::NoCapability);
            else if (actor->team >= 0 && round_context_.support_ap[actor->team] <= 0)
                result.failure_mask = static_cast<uint32_t>(GateFailureReason::OutOfAmmo);
            if (result.allowed && actor->team >= 0) {
                round_context_.support_ap[actor->team] -= 1;
                round_context_.support_ap_spent[actor->team] += 1;
            }
            break;
        }
        case ActionType::EngageTrack: {
            ScenarioEntity* actor = find_entity(req.actor);
            const Scenario::EffectProfile* profile = find_effect_profile(req.effect_profile_index);
            ScenarioEntity* target = find_entity(req.track_target);
            const Track* target_track = nullptr;
            if (belief_it != beliefs_.states().end())
                target_track = belief_it->second.find_track(req.track_target);

            // Basic runtime checks that do not require spatial adjudication.
            uint32_t failure = 0;
            if (!actor || !actor->can_engage)
                failure |= static_cast<uint32_t>(GateFailureReason::NoCapability);
            if (actor && round_context_.operator_ap[actor->id] <= 0)
                failure |= static_cast<uint32_t>(GateFailureReason::OutOfAmmo);
            if (actor && actor->cooldown_ticks_remaining > 0)
                failure |= static_cast<uint32_t>(GateFailureReason::Cooldown);
            if (!profile)
                failure |= static_cast<uint32_t>(GateFailureReason::NoCapability);
            if (actor && profile && actor->ammo < profile->ammo_cost)
                failure |= static_cast<uint32_t>(GateFailureReason::OutOfAmmo);
            if (!target_track)
                failure |= static_cast<uint32_t>(GateFailureReason::TrackNotFound);

            // Two-stage gating model:
            //  1) Decision gate uses actor-accessible belief data only.
            //  2) Truth adjudication runs only if decision gate passed and checks realization truth.
            uint32_t belief_failure = 0;
            uint32_t truth_failure = 0;

            if (actor && target_track) {
                EngagementGateInputs gate_inputs;
                gate_inputs.actor = actor;
                gate_inputs.target_track = target_track;
                gate_inputs.effect_profile_index = req.effect_profile_index;
                gate_inputs.effect_profile = profile;
                gate_inputs.engagement_rules = scn_ ? &scn_->engagement_rules : nullptr;
                gate_inputs.world.map = &map_;
                gate_inputs.world.tick = tick;
                gate_inputs.world.entities = &entities_;
                gate_inputs.world.actor_id = req.actor;
                gate_inputs.world.track_target_id = req.track_target;

                const EngagementGateResult belief_gate = compute_engagement_gates(
                    gate_inputs, EngagementGateStage::DecisionFromBelief);
                belief_failure = belief_gate.failure_mask;
                failure |= belief_failure;

                if (belief_failure == 0) {
                    gate_inputs.target_truth = target;
                    const EngagementGateResult truth_gate = compute_engagement_gates(
                        gate_inputs, EngagementGateStage::TruthAdjudication);
                    truth_failure = truth_gate.failure_mask;
                    failure |= truth_failure;
                }
            }

            result.failure_mask = failure;
            result.belief_failure_mask = belief_failure;
            result.truth_failure_mask = truth_failure;
            result.rejected_by_belief_gate = (belief_failure != 0);
            result.rejected_by_truth_adjudication =
                (belief_failure == 0 && truth_failure != 0);
            result.allowed = (failure == 0);

            if (result.allowed && actor && profile && target) {
                EffectOutcome outcome;
                outcome.request = req;
                outcome.tick = tick;
                outcome.realized = true;
                outcome.vitality_before = target->vitality;
                outcome.actor_ammo_before = actor->ammo;
                outcome.actor_cooldown_before = actor->cooldown_ticks_remaining;

                outcome.hit = rng_.uniform() < profile->hit_probability;
                if (outcome.hit) {
                    int delta = profile->vitality_delta_min;
                    if (profile->vitality_delta_min != profile->vitality_delta_max) {
                        float u = rng_.uniform();
                        int span = profile->vitality_delta_max - profile->vitality_delta_min + 1;
                        delta = profile->vitality_delta_min + static_cast<int>(std::floor(u * span));
                        if (delta > profile->vitality_delta_max)
                            delta = profile->vitality_delta_max;
                    }
                    int new_vitality = std::clamp(target->vitality + delta, 0, target->max_vitality);
                    target->vitality = new_vitality;
                }

                actor->ammo = std::max(0, actor->ammo - profile->ammo_cost);
                round_context_.operator_ap[actor->id] -= 1;
                actor->cooldown_ticks_remaining = profile->cooldown_ticks;

                outcome.vitality_after = target->vitality;
                outcome.vitality_delta = outcome.vitality_after - outcome.vitality_before;
                outcome.actor_ammo_after = actor->ammo;
                outcome.actor_cooldown_after = actor->cooldown_ticks_remaining;

                if (game_mode_ && outcome.vitality_delta != 0)
                    game_mode_->on_entity_damaged(tick, req.track_target,
                                                  outcome.vitality_before,
                                                  outcome.vitality_after);

                hooks.on_effect_resolved(tick, outcome);
            }
            break;
        }
        case ActionType::RequestBDA: {
            result.allowed = false;
            result.failure_mask = static_cast<uint32_t>(GateFailureReason::NoCapability);
            break;
        }
        }

        hooks.on_action_resolved(tick, result);
    }
}
