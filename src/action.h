#pragma once

#include "grid.h"
#include "types.h"

#include <cstdint>

// Per-activation action request submitted to SimEngine::step_activation.
// Contract §4 action types plus EndTurn (give up remaining AP and yield to
// the next operator). See turn_based_tactical_contract.md §4.
enum class ActionKind : uint8_t {
    Move,
    Shoot,
    Overwatch,
    Peek,
    OpenDoor,
    CloseDoor,
    Breach,
    DeployDrone,
    Interact,
    HackDevice,
    EndTurn,
};

struct ActionRequest {
    ActionKind kind = ActionKind::EndTurn;
    GridPos target_cell{};          // Move, Peek, OpenDoor, CloseDoor, Breach, DeployDrone
    EntityId target_entity = 0;     // Shoot

    static ActionRequest move(GridPos cell) {
        ActionRequest r; r.kind = ActionKind::Move; r.target_cell = cell; return r;
    }
    static ActionRequest shoot(EntityId target) {
        ActionRequest r; r.kind = ActionKind::Shoot; r.target_entity = target; return r;
    }
    static ActionRequest overwatch() {
        ActionRequest r; r.kind = ActionKind::Overwatch; return r;
    }
    static ActionRequest peek(GridPos cell) {
        ActionRequest r; r.kind = ActionKind::Peek; r.target_cell = cell; return r;
    }
    static ActionRequest open_door(GridPos cell) {
        ActionRequest r; r.kind = ActionKind::OpenDoor; r.target_cell = cell; return r;
    }
    static ActionRequest close_door(GridPos cell) {
        ActionRequest r; r.kind = ActionKind::CloseDoor; r.target_cell = cell; return r;
    }
    static ActionRequest breach(GridPos cell) {
        ActionRequest r; r.kind = ActionKind::Breach; r.target_cell = cell; return r;
    }
    static ActionRequest deploy_drone(GridPos cell) {
        ActionRequest r; r.kind = ActionKind::DeployDrone; r.target_cell = cell; return r;
    }
    static ActionRequest interact() {
        ActionRequest r; r.kind = ActionKind::Interact; return r;
    }
    static ActionRequest hack_device(GridPos cell) {
        ActionRequest r; r.kind = ActionKind::HackDevice; r.target_cell = cell; return r;
    }
    static ActionRequest end_turn() {
        ActionRequest r; r.kind = ActionKind::EndTurn; return r;
    }
};

inline const char* action_kind_str(ActionKind k) {
    switch (k) {
        case ActionKind::Move:        return "move";
        case ActionKind::Shoot:       return "shoot";
        case ActionKind::Overwatch:   return "overwatch";
        case ActionKind::Peek:        return "peek";
        case ActionKind::OpenDoor:    return "open_door";
        case ActionKind::CloseDoor:   return "close_door";
        case ActionKind::Breach:      return "breach";
        case ActionKind::DeployDrone: return "deploy_drone";
        case ActionKind::Interact:    return "interact";
        case ActionKind::HackDevice:  return "hack_device";
        case ActionKind::EndTurn:     return "end_turn";
    }
    return "???";
}
