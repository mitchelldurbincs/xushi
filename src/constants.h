#pragma once

#include "types.h"
#include <cmath>

// ---------------------------------------------------------------------------
// Floating-point geometry
// ---------------------------------------------------------------------------

// kEpsilon (1e-9f) is defined in types.h alongside Vec2, since Vec2 uses it
// inline and types.h cannot include this header.

// Tau (2*pi) -- full circle in radians.
inline constexpr float kTau = 2.0f * 3.14159265f;

// ---------------------------------------------------------------------------
// Phantom (false-positive) observation parameters
// ---------------------------------------------------------------------------

// Sentinel entity ID indicating a phantom observation that does not
// correspond to any real entity.
inline constexpr EntityId kPhantomTargetId = 0xFFFFFFFF;

// Uncertainty radius (meters) assigned to phantom observations.
inline constexpr float kPhantomUncertainty = 2.0f;

// Phantom confidence is drawn uniformly from
//   [kPhantomConfidenceMin, kPhantomConfidenceMin + kPhantomConfidenceRange].
inline constexpr float kPhantomConfidenceMin   = 0.2f;
inline constexpr float kPhantomConfidenceRange = 0.3f;

// ---------------------------------------------------------------------------
// Task system
// ---------------------------------------------------------------------------

// Radius (meters) within which an entity is considered to have arrived at a
// task target. This is intentionally larger than kArrivalRadiusSq (movement.h,
// 1 m^2 i.e. ~1 m) because task verification only requires proximity to an
// area of interest, whereas waypoint navigation requires precise arrival.
inline constexpr float kTaskArrivalRadius = 5.0f;

// Tracks with confidence >= this threshold are not considered for VERIFY tasks.
inline constexpr float kTaskConfidenceThreshold = 0.5f;

// ---------------------------------------------------------------------------
// Search initialization
// ---------------------------------------------------------------------------

// Large sentinel value used to initialize nearest-entity distance searches.
inline constexpr float kInfDistance = 1e9f;
