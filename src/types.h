#pragma once

#include <cmath>
#include <cstdint>

// Near-zero threshold for distance and normalization checks.
// Defined here (rather than constants.h) because Vec2 uses it inline
// and constants.h depends on types defined in this header.
inline constexpr float kEpsilon = 1e-9f;

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;

    Vec2 operator+(Vec2 other) const { return {x + other.x, y + other.y}; }
    Vec2 operator-(Vec2 other) const { return {x - other.x, y - other.y}; }
    Vec2 operator*(float s) const { return {x * s, y * s}; }

    float length() const { return std::sqrt(x * x + y * y); }

    Vec2 normalized() const {
        float len = length();
        if (len < kEpsilon) return {0.0f, 0.0f};
        return {x / len, y / len};
    }
};

using EntityId = uint32_t;
