#pragma once

#include <cstdint>
#include <cmath>

struct Rng {
    uint64_t state;

    explicit Rng(uint64_t seed) : state(seed) {}

    uint64_t next() {
        // splitmix64
        state += 0x9e3779b97f4a7c15ULL;
        uint64_t z = state;
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        return z ^ (z >> 31);
    }

    // Returns a float in [0, 1)
    float uniform() {
        return static_cast<float>(next() >> 40) / static_cast<float>(1ULL << 24);
    }

    // Approximate standard normal via Box-Muller transform
    float normal() {
        float u1 = uniform();
        float u2 = uniform();
        // Clamp u1 away from zero to avoid log(0)
        if (u1 < 1e-10f) u1 = 1e-10f;
        return std::sqrt(-2.0f * std::log(u1)) * std::cos(2.0f * 3.14159265358979f * u2);
    }
};
