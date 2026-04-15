#pragma once

#include "types.h"
#include <cstdint>
#include <vector>

// ── Action types ──

enum class ActionType {
    DesignateTrack,
    ClearDesignation,
    EngageTrack,
    RequestBDA
};

enum class ActionActorType {
    Auto,
    Operator,
    Drone,
    TeamSupportController
};

enum class ActionPhaseKind {
    Any,
    OperatorActivation,
    SupportPhase
};

enum class ActionRejectionReason {
    None = 0,
    IllegalPhase,
    IllegalActorType,
    InvalidActor,
    InvalidTarget,
    InsufficientAP,
    InsufficientSAP,
    OneShotAlreadyUsed,
    UnsupportedAction
};

struct OperatorActivationActionDef {
    int ap_cost = 0;
    bool one_shot = false;
};

struct SupportPhaseActionDef {
    int sap_cost = 0;
    bool spend_team_pool = true;
    int initiative_delta = 0;
};

enum class DesignationKind {
    Observe,
    Verify,
    MaintainCustody,
    Engage,
    BDA
};

// Bitmask: multiple reasons can fire simultaneously.
enum class GateFailureReason : uint32_t {
    None              = 0,
    NoCapability      = 1u << 0,
    Cooldown          = 1u << 1,
    OutOfAmmo         = 1u << 2,
    TrackTooStale     = 1u << 3,
    TrackTooUncertain = 1u << 4,
    IdentityTooWeak   = 1u << 5,
    NeedsCorroboration= 1u << 6,
    OutOfRange        = 1u << 7,
    NoLineOfEffect    = 1u << 8,
    ProtectedZone     = 1u << 9,
    FriendlyRisk      = 1u << 10,
    ActorDisabled     = 1u << 11,
    ROEBlocked        = 1u << 12,
    TrackNotFound     = 1u << 13,
    FriendlyTarget    = 1u << 14
};

inline uint32_t operator|(GateFailureReason a, GateFailureReason b) {
    return static_cast<uint32_t>(a) | static_cast<uint32_t>(b);
}
inline uint32_t operator|(uint32_t a, GateFailureReason b) {
    return a | static_cast<uint32_t>(b);
}
inline bool has_reason(uint32_t mask, GateFailureReason r) {
    return (mask & static_cast<uint32_t>(r)) != 0;
}

// ── Request / Result ──

struct ActionRequest {
    EntityId actor = 0;
    ActionActorType actor_type = ActionActorType::Auto;
    ActionType type = ActionType::DesignateTrack;
    ActionPhaseKind phase_kind = ActionPhaseKind::Any;
    EntityId track_target = 0;              // Track.target EntityId
    DesignationKind desig_kind = DesignationKind::Observe;  // for DesignateTrack
    int priority = 0;                       // for DesignateTrack
    uint32_t effect_profile_index = 0;      // for EngageTrack (future)
    OperatorActivationActionDef operator_def{};
    SupportPhaseActionDef support_def{};
};

struct ActionResult {
    ActionRequest request;
    bool allowed = false;
    uint32_t failure_mask = 0;  // GateFailureReason bits
    // Stage-specific telemetry for EngageTrack replay/debugging.
    uint32_t belief_failure_mask = 0;
    uint32_t truth_failure_mask = 0;
    bool rejected_by_belief_gate = false;
    bool rejected_by_truth_adjudication = false;
    std::vector<ActionRejectionReason> rejection_reasons;
    int tick = 0;
};

inline bool has_rejection_reason(const ActionResult& result, ActionRejectionReason reason) {
    for (ActionRejectionReason r : result.rejection_reasons)
        if (r == reason)
            return true;
    return false;
}

struct EffectOutcome {
    ActionRequest request;
    bool realized = false;          // false => gate rejected/no realized effect
    bool hit = false;
    int vitality_before = 0;
    int vitality_after = 0;
    int vitality_delta = 0;         // signed change applied to target
    int actor_ammo_before = 0;
    int actor_ammo_after = 0;
    int actor_cooldown_before = 0;
    int actor_cooldown_after = 0;
    int tick = 0;
};

// ── Designation records ──

struct DesignationRecord {
    uint64_t designation_id = 0;
    EntityId issuer = 0;
    EntityId track_target = 0;
    DesignationKind kind = DesignationKind::Observe;
    int priority = 0;
    int created_tick = 0;
    int expires_tick = 0;
};

// ── String helpers (for replay/debug) ──

inline const char* action_type_str(ActionType t) {
    switch (t) {
        case ActionType::DesignateTrack:   return "DESIGNATE_TRACK";
        case ActionType::ClearDesignation: return "CLEAR_DESIGNATION";
        case ActionType::EngageTrack:      return "ENGAGE_TRACK";
        case ActionType::RequestBDA:       return "REQUEST_BDA";
    }
    return "???";
}

inline const char* designation_kind_str(DesignationKind k) {
    switch (k) {
        case DesignationKind::Observe:         return "OBSERVE";
        case DesignationKind::Verify:          return "VERIFY";
        case DesignationKind::MaintainCustody: return "MAINTAIN_CUSTODY";
        case DesignationKind::Engage:          return "ENGAGE";
        case DesignationKind::BDA:             return "BDA";
    }
    return "???";
}

inline const char* action_actor_type_str(ActionActorType t) {
    switch (t) {
        case ActionActorType::Auto: return "AUTO";
        case ActionActorType::Operator: return "OPERATOR";
        case ActionActorType::Drone: return "DRONE";
        case ActionActorType::TeamSupportController: return "TEAM_SUPPORT_CONTROLLER";
    }
    return "???";
}

inline const char* action_phase_kind_str(ActionPhaseKind k) {
    switch (k) {
        case ActionPhaseKind::Any: return "ANY";
        case ActionPhaseKind::OperatorActivation: return "OPERATOR_ACTIVATION";
        case ActionPhaseKind::SupportPhase: return "SUPPORT_PHASE";
    }
    return "???";
}

inline const char* action_rejection_reason_str(ActionRejectionReason reason) {
    switch (reason) {
        case ActionRejectionReason::None: return "NONE";
        case ActionRejectionReason::IllegalPhase: return "ILLEGAL_PHASE";
        case ActionRejectionReason::IllegalActorType: return "ILLEGAL_ACTOR_TYPE";
        case ActionRejectionReason::InvalidActor: return "INVALID_ACTOR";
        case ActionRejectionReason::InvalidTarget: return "INVALID_TARGET";
        case ActionRejectionReason::InsufficientAP: return "INSUFFICIENT_AP";
        case ActionRejectionReason::InsufficientSAP: return "INSUFFICIENT_SAP";
        case ActionRejectionReason::OneShotAlreadyUsed: return "ONE_SHOT_ALREADY_USED";
        case ActionRejectionReason::UnsupportedAction: return "UNSUPPORTED_ACTION";
    }
    return "???";
}
