#pragma once

#include "types.h"

// -----------------------------------------------------------------------------
// Turn-based tactical constants.
// See turn_based_tactical_contract.md for the authoritative values.
// -----------------------------------------------------------------------------

// Default number of rounds in a scenario (can be overridden by scenario JSON).
inline constexpr int kDefaultRounds = 12;

// Operator stats (contract §4).
inline constexpr int kOperatorMaxAp = 3;
inline constexpr int kOperatorMaxHp = 100;
inline constexpr int kOperatorMaxAmmo = 10;
inline constexpr int kOperatorVisionRange = 10;

// Per-team support AP pool (contract §2 Phase 2).
inline constexpr int kTeamSupportApMax = 2;

// Sentinel entity ID representing an invalid / unset entity reference.
inline constexpr EntityId kInvalidEntity = 0xFFFFFFFEu;
