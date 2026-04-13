#include "engagement.h"

#include "constants.h"

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

EngagementGateResult compute_engagement_gates(const EngagementGateInputs& in) {
    EngagementGateResult out;

    // Placeholder gates - not modeled yet but preserved for compatibility
    const bool actor_has_capability = false;
    if (!actor_has_capability)
        out.failure_mask |= static_cast<uint32_t>(GateFailureReason::NoCapability);

    const bool cooldown_available = true;
    if (!cooldown_available)
        out.failure_mask |= static_cast<uint32_t>(GateFailureReason::Cooldown);

    const bool ammo_available = true;
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

    if (in.target_track->identity_confidence < kMinIdentityConfidence)
        out.failure_mask |= static_cast<uint32_t>(GateFailureReason::IdentityTooWeak);

    if (in.target_track->corroboration_count < kMinCorroborationCount)
        out.failure_mask |= static_cast<uint32_t>(GateFailureReason::NeedsCorroboration);

    if (!in.target_truth) {
        out.failure_mask |= static_cast<uint32_t>(GateFailureReason::TrackNotFound);
        return out;
    }

    const float actor_to_target = (in.target_truth->position - in.actor->position).length();
    out.debug.actor_to_truth_distance = actor_to_target;

    const float max_effect_range = effect_profile_range(in.effect_profile_index);
    if (actor_to_target > max_effect_range)
        out.failure_mask |= static_cast<uint32_t>(GateFailureReason::OutOfRange);

    if (in.world.map) {
        const bool has_loe = in.world.map->line_of_sight(in.actor->position, in.target_truth->position);
        out.debug.has_line_of_effect = has_loe;
        if (!has_loe)
            out.failure_mask |= static_cast<uint32_t>(GateFailureReason::NoLineOfEffect);
    }

    // Protected zone check
    float protected_zone_distance = (in.target_truth->position - kProtectedZoneCenter).length();
    if (protected_zone_distance <= kProtectedZoneRadius)
        out.failure_mask |= static_cast<uint32_t>(GateFailureReason::ProtectedZone);

    // Friendly risk check
    if (in.world.entities) {
        for (const auto& entity : *in.world.entities) {
            if (entity.id == in.world.actor_id || entity.id == in.world.track_target_id)
                continue;
            if ((entity.position - in.target_truth->position).length() <= kFriendlyRiskRadius) {
                out.failure_mask |= static_cast<uint32_t>(GateFailureReason::FriendlyRisk);
                break;
            }
        }
    }

    // ROE gate placeholder
    const bool roe_allows = true;
    if (!roe_allows)
        out.failure_mask |= static_cast<uint32_t>(GateFailureReason::ROEBlocked);

    return out;
}
