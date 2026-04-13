#include "engagement.h"

#include "constants.h"
#include <algorithm>

namespace {
constexpr float kDefaultEffectRange = 80.0f;
constexpr float kEffectRangeStep = 20.0f;
constexpr float kMaxTrackUncertainty = 20.0f;
constexpr float kMinIdentityConfidence = 0.5f;
constexpr int kMinCorroborationCount = 1;
constexpr Vec2 kProtectedZoneCenter{0.0f, 0.0f};
constexpr float kProtectedZoneRadius = 10.0f;
constexpr float kFriendlyRiskRadius = 8.0f;

float effect_profile_range(uint32_t effect_profile_index) {
    return kDefaultEffectRange + static_cast<float>(effect_profile_index) * kEffectRangeStep;
}
}

EngagementGateResult compute_engagement_gates(const EngagementGateInputs& in,
                                              EngagementGateStage stage) {
    EngagementGateResult out;

    // Capability: actor must be able to engage and have the requested effect profile
    const bool actor_has_capability = in.actor && in.actor->can_engage &&
        std::find(in.actor->allowed_effect_profile_indices.begin(),
                  in.actor->allowed_effect_profile_indices.end(),
                  static_cast<int>(in.effect_profile_index))
            != in.actor->allowed_effect_profile_indices.end();
    if (!actor_has_capability)
        out.failure_mask |= static_cast<uint32_t>(GateFailureReason::NoCapability);

    // Cooldown: actor must not be on cooldown
    const bool cooldown_available = !in.actor || in.actor->cooldown_ticks_remaining == 0;
    if (!cooldown_available)
        out.failure_mask |= static_cast<uint32_t>(GateFailureReason::Cooldown);

    // Ammo: actor must have enough ammo for the effect profile
    const int ammo_needed = (in.effect_profile) ? in.effect_profile->ammo_cost : 1;
    const bool ammo_available = !in.actor || in.actor->ammo >= ammo_needed;
    if (!ammo_available)
        out.failure_mask |= static_cast<uint32_t>(GateFailureReason::OutOfAmmo);

    if (!in.actor) {
        out.failure_mask |= static_cast<uint32_t>(GateFailureReason::ActorDisabled);
        return out;
    }

    if (!in.target_track) {
        out.failure_mask |= static_cast<uint32_t>(GateFailureReason::TrackNotFound);
        return out;
    }

    out.debug.track_age_ticks = in.world.tick - in.target_track->last_update_tick;
    out.debug.track_uncertainty = in.target_track->uncertainty;
    out.debug.track_identity_confidence = in.target_track->identity_confidence;
    out.debug.track_corroboration_count = in.target_track->corroboration_count;

    if (in.target_track->status != TrackStatus::FRESH)
        out.failure_mask |= static_cast<uint32_t>(GateFailureReason::TrackTooStale);

    if (in.target_track->uncertainty > kMaxTrackUncertainty)
        out.failure_mask |= static_cast<uint32_t>(GateFailureReason::TrackTooUncertain);

    // Identity threshold: use effect profile if available, otherwise fallback
    const float identity_threshold = in.effect_profile
        ? in.effect_profile->identity_threshold : kMinIdentityConfidence;
    if (in.target_track->identity_confidence < identity_threshold)
        out.failure_mask |= static_cast<uint32_t>(GateFailureReason::IdentityTooWeak);

    // Corroboration threshold: use effect profile if available, otherwise fallback
    const float corroboration_threshold = in.effect_profile
        ? in.effect_profile->corroboration_threshold
        : static_cast<float>(kMinCorroborationCount);
    if (static_cast<float>(in.target_track->corroboration_count) < corroboration_threshold)
        out.failure_mask |= static_cast<uint32_t>(GateFailureReason::NeedsCorroboration);

    // Range: use effect profile if available, otherwise fallback formula
    const float max_effect_range = in.effect_profile
        ? in.effect_profile->range : effect_profile_range(in.effect_profile_index);

    // Stage 1 (DecisionFromBelief): use only actor-accessible information.
    // Geometry checks are estimated from the track state (not truth).
    if (stage == EngagementGateStage::DecisionFromBelief) {
        const float actor_to_track = (in.target_track->estimated_position - in.actor->position).length();
        out.debug.actor_to_track_distance = actor_to_track;
        if (actor_to_track > max_effect_range)
            out.failure_mask |= static_cast<uint32_t>(GateFailureReason::OutOfRange);

        const bool check_los = in.effect_profile ? in.effect_profile->requires_los : true;
        if (check_los && in.world.map) {
            const bool predicted_has_loe =
                in.world.map->line_of_sight(in.actor->position, in.target_track->estimated_position);
            out.debug.predicted_line_of_effect = predicted_has_loe;
            if (!predicted_has_loe)
                out.failure_mask |= static_cast<uint32_t>(GateFailureReason::NoLineOfEffect);
        }

        // Decision-phase ROE placeholder (policy known to actor, not truth lookup).
        const bool roe_allows = true;
        if (!roe_allows)
            out.failure_mask |= static_cast<uint32_t>(GateFailureReason::ROEBlocked);
        return out;
    }

    // Stage 2 (TruthAdjudication): resolve truth-dependent geometry/policy before realization.
    if (!in.target_truth) {
        out.failure_mask |= static_cast<uint32_t>(GateFailureReason::TrackNotFound);
        return out;
    }

    const float actor_to_target = (in.target_truth->position - in.actor->position).length();
    out.debug.actor_to_truth_distance = actor_to_target;
    if (actor_to_target > max_effect_range)
        out.failure_mask |= static_cast<uint32_t>(GateFailureReason::OutOfRange);

    const bool check_los = in.effect_profile ? in.effect_profile->requires_los : true;
    if (check_los && in.world.map) {
        const bool has_loe = in.world.map->line_of_sight(in.actor->position, in.target_truth->position);
        out.debug.has_line_of_effect = has_loe;
        if (!has_loe)
            out.failure_mask |= static_cast<uint32_t>(GateFailureReason::NoLineOfEffect);
    }

    const float protected_zone_distance = (in.target_truth->position - kProtectedZoneCenter).length();
    if (protected_zone_distance <= kProtectedZoneRadius)
        out.failure_mask |= static_cast<uint32_t>(GateFailureReason::ProtectedZone);

    if (in.actor->team >= 0 && in.target_truth->team >= 0 &&
        in.actor->team == in.target_truth->team) {
        out.failure_mask |= static_cast<uint32_t>(GateFailureReason::FriendlyTarget);
    }

    if (in.world.entities) {
        for (const auto& entity : *in.world.entities) {
            if (entity.id == in.world.actor_id || entity.id == in.world.track_target_id)
                continue;
            if (entity.team >= 0 && in.actor->team >= 0 && entity.team != in.actor->team)
                continue;
            if ((entity.position - in.target_truth->position).length() <= kFriendlyRiskRadius) {
                out.failure_mask |= static_cast<uint32_t>(GateFailureReason::FriendlyRisk);
                break;
            }
        }
    }

    const bool roe_allows = true;
    if (!roe_allows)
        out.failure_mask |= static_cast<uint32_t>(GateFailureReason::ROEBlocked);

    return out;
}
