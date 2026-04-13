# xushi 虚实

Multi-agent tactical simulation engine — sensor detection, belief tracking, lossy comms, and weapons engagement in a deterministic tick loop.

> 虚实 (xūshí) is a concept from Sun Tzu's *Art of War* — "the empty and the full," or "the false and the real." It describes the gap between what appears to be true and what actually is. This simulation models exactly that: agents build a perceived picture from noisy sensors and lossy comms, while ground truth remains separate and hidden.

## What It Does

- **Tick-based discrete simulation** — configurable timestep and seed, fully deterministic across platforms
- **Multi-agent architecture** — sensors detect, trackers maintain belief states, engagers fire
- **Sensing model** — line-of-sight with rectangular obstacles, range-dependent confidence and position noise, miss rates, false positives, class confusion
- **Belief tracking** — track lifecycle (FRESH → STALE → EXPIRED), confidence decay, uncertainty growth, negative evidence, corroboration from multiple sources
- **Lossy communications** — message latency (base + distance-scaled), probabilistic message loss
- **Engagement system** — 14 tactical gates evaluated before any shot (range, LOS, staleness, uncertainty, identity confidence, corroboration, ammo, cooldown, ROE, and more)
- **Movement** — constant velocity, waypoint navigation (loop/stop/branch modes), patrol policies
- **Task system** — automatic VERIFY task assignment for degraded tracks
- **Deterministic replay** — NDJSON event log, byte-for-byte identical across runs with same seed
- **Replay viewer** — optional raylib-based interactive visualization

## Quick Start

**Requirements:** C++17 compiler (GCC, Clang, Apple Clang, or MSVC), CMake 3.20+. No external dependencies for core.

**Linux / macOS:**
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

**Windows (Visual Studio 2022):**
```bash
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release -j %NUMBER_OF_PROCESSORS%
```

```bash
# Run a scenario
./build/xushi scenarios/default.json          # Linux/macOS
build\Release\xushi.exe scenarios\default.json  # Windows

# Benchmark mode (suppresses per-tick output)
./build/xushi --bench scenarios/default.json
```

The simulation writes a `.replay` file alongside the scenario (e.g., `scenarios/default.replay`).

### Run Tests

```bash
ctest --test-dir build --output-on-failure
```

## Scenarios

### Included Scenarios

| File | Description |
|------|-------------|
| `default.json` | Basic 3-entity setup with obstacle and engagement profile |
| `multi_agent.json` | Multiple sensors and trackers |
| `dual_patrol.json` | Two-sensor patrol coverage |
| `waypoint_patrol.json` | Target following a waypoint loop |
| `branch_waypoint.json` | Stochastic waypoint branching |
| `los_blocked.json` | Obstacles blocking sensor LOS |
| `noisy_perception.json` | Miss rate and false positives enabled |
| `distance_comms.json` | Distance-scaled communication latency |
| `task_verify.json` | VERIFY task assignment |
| `mixed_era.json` | Mixed-capability entities on same unit |
| `patrol_policy.json` | Policy-driven patrol routes |
| `benchmark_dense.json` | 1000-tick stress test with 5 targets and 8 obstacles |

### Configuration Reference

All scenarios are JSON files. Top-level fields:

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `seed` | int | *required* | RNG seed for determinism |
| `dt` | float | `1.0` | Timestep in seconds |
| `ticks` | int | `100` | Simulation duration |
| `max_sensor_range` | float | `80.0` | Maximum detection range (meters) |
| `obstacles` | array | `[]` | Rectangular LOS blockers: `{"min": [x,y], "max": [x,y]}` |
| `entities` | array | *required* | Entity definitions (see below) |
| `channel` | object | see below | Communication channel config |
| `belief` | object | see below | Track lifecycle config |
| `perception` | object | see below | Sensor noise config |
| `effect_profiles` | array | `[]` | Weapons/engagement profiles |
| `policy` | object | none | Movement policy config |

A valid scenario requires at least one entity with `can_sense`, one with `can_track`, and one with `is_observable`.

### Entity Fields

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `id` | int | *required* | Unique entity ID |
| `type` | string | *required* | Display name / role label |
| `pos` | [x, y] | *required* | Starting position |
| `vel` | [x, y] | *required* | Initial velocity |
| `can_sense` | bool | `false` | Runs sensing pass |
| `can_track` | bool | `false` | Maintains belief state |
| `is_observable` | bool | `false` | Can be detected by sensors |
| `can_engage` | bool | `false` | Can execute engagement actions |
| `class_id` | int | `0` | Ground truth class for identification |
| `vitality` | int | `100` | Current health points |
| `max_vitality` | int | `100` | Maximum health points |
| `ammo` | int | `0` | Ammunition remaining |
| `cooldown_ticks_remaining` | int | `0` | Ticks until next engagement allowed |
| `allowed_effect_profile_indices` | int[] | `[]` | Indices into `effect_profiles` |
| `waypoints` | [[x,y], ...] | none | Waypoint path (requires `speed`) |
| `speed` | float | — | Movement speed (required with waypoints) |
| `waypoint_mode` | string | `"stop"` | `"stop"` or `"loop"` |
| `branch_points` | object | none | `{"waypoint_idx": [successor_indices]}` |

### Channel Config

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `base_latency` | int | `3` | Minimum delivery delay (ticks) |
| `per_distance` | float | `0.0` | Additional latency per meter |
| `loss` | float | `0.1` | Message drop probability [0, 1] |

### Belief Config

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `fresh_ticks` | int | `5` | Ticks a track stays FRESH |
| `stale_ticks` | int | `10` | Ticks a track stays STALE before expiry |
| `uncertainty_growth_per_second` | float | `0.5` | Position uncertainty growth (m/s) |
| `confidence_decay_per_second` | float | `0.05` | Confidence decay rate |
| `negative_evidence_factor` | float | `0.3` | Confidence reduction when in sensor range but undetected |

### Perception Config

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `miss_rate` | float | `0.0` | Detection miss probability [0, 1] |
| `false_positive_rate` | float | `0.0` | Phantom detection rate per sensor per tick |
| `class_confusion_rate` | float | `0.0` | Probability of misidentifying target class |

### Effect Profiles

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `name` | string | *required* | Profile identifier |
| `range` | float | *required* | Maximum engagement range |
| `requires_los` | bool | `false` | Require line of effect to target |
| `identity_threshold` | float | `0.0` | Minimum identity confidence to fire |
| `corroboration_threshold` | float | `0.0` | Minimum corroboration to fire |
| `hit_probability` | float | `1.0` | Probability of hit on engagement |
| `vitality_delta_min` | int | `0` | Min vitality change (negative = damage) |
| `vitality_delta_max` | int | `= min` | Max vitality change |
| `cooldown_ticks` | int | `0` | Cooldown imposed after firing |
| `ammo_cost` | int | `0` | Ammo consumed per engagement |
| `roe_flags` | string[] | `[]` | Rules of engagement constraint flags |

### Policy Config

Currently supports the `patrol` type:

```json
"policy": {
  "type": "patrol",
  "routes": {
    "0": [[10, 10], [90, 10], [90, 50], [10, 50]]
  }
}
```

Routes map entity ID (as string key) to a list of patrol waypoints.

## Architecture

### Tick Loop

Each simulation tick executes these phases in order:

1. **Cooldowns** — decrement engagement cooldown timers
2. **Movement** — task override > policy override > waypoint/velocity
3. **Sensing** — LOS + range checks, generate noisy observations, send messages to trackers, apply negative evidence, generate false positives
4. **Communication** — deliver messages whose latency has elapsed
5. **Belief** — integrate observations into tracks, decay confidence, grow uncertainty, expire old tracks
6. **Actions** — adjudicate pending action requests (designate, clear, engage, BDA)
7. **Tasks** — check task completion (arrival + corroboration), assign VERIFY tasks for degraded tracks
8. **World hash** — every 10 ticks, FNV-1a hash of all positions + belief states

### Track Lifecycle

```
Detection → FRESH ──(fresh_ticks)──→ STALE ──(stale_ticks)──→ EXPIRED (removed)
             confidence ≈ 1.0         confidence decays          confidence → 0
             uncertainty low           uncertainty grows
```

Tracks are updated by new observations, which reset confidence and position estimates. Negative evidence (target not detected while in sensor range) actively reduces confidence.

### Engagement Gates

Before an `EngageTrack` action is allowed, 14 gates are evaluated as a bitmask — multiple can fail simultaneously:

`NoCapability` · `Cooldown` · `OutOfAmmo` · `TrackTooStale` · `TrackTooUncertain` · `IdentityTooWeak` · `NeedsCorroboration` · `OutOfRange` · `NoLineOfEffect` · `ProtectedZone` · `FriendlyRisk` · `ActorDisabled` · `ROEBlocked` · `TrackNotFound`

### Source Layout

```
src/
  main.cpp              CLI entry point with replay logging
  sim_engine.{h,cpp}    tick loop, action adjudication
  sim.{h,cpp}           high-level sim interface
  scenario.{h,cpp}      JSON scenario loading + validation
  sensing.{h,cpp}       LOS detection, noise model
  belief.{h,cpp}        track management, decay, negative evidence
  comm.{h,cpp}          message queuing, latency, loss
  engagement.{h,cpp}    tactical gate evaluation
  movement.h            waypoint navigation, constant velocity
  patrol_policy.h       patrol policy implementation
  policy.h              policy interface
  action.h              action types, gate failure bitmask
  task.h                task definitions (VERIFY)
  map.{h,cpp}           obstacle map, LOS raycasting
  replay.{h,cpp}        NDJSON replay writer/reader
  replay_events.h       event serialization factories
  json.{h,cpp}          zero-dependency JSON parser
  rng.h                 splitmix64 RNG
  types.h               Vec2, EntityId
  constants.h           tuning constants
  invariants.h          debug assertions
  stats.{h,cpp}         performance counters
  path_resolve.{h,cpp}  replay path resolution

viewer/
  main.cpp              viewer entry point
  viewer.h              state machine, rendering
  viewer_load.cpp       replay file loading
  viewer_update.cpp     input handling, playback controls
  viewer_draw.cpp       raylib rendering
```

## Viewer

Optional interactive replay visualizer. Requires [raylib](https://github.com/raysan5/raylib) 5.5.

**Linux / macOS:**
```bash
git clone --depth 1 --branch 5.5 https://github.com/raysan5/raylib.git /tmp/raylib
cmake -B /tmp/raylib/build -S /tmp/raylib -DCMAKE_BUILD_TYPE=Release -DBUILD_EXAMPLES=OFF
cmake --build /tmp/raylib/build -j$(nproc)
sudo cmake --install /tmp/raylib/build

# Rebuild xushi (CMake auto-detects raylib)
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/xushi_viewer scenarios/default.replay
```

**Windows (Visual Studio 2022):**
```bash
git clone --depth 1 --branch 5.5 https://github.com/raysan5/raylib.git raylib-5.5
cmake -B raylib-5.5/build -S raylib-5.5 -G "Visual Studio 17 2022" -A x64 -DBUILD_EXAMPLES=OFF
cmake --build raylib-5.5/build --config Release
cmake --install raylib-5.5/build --config Release --prefix raylib-5.5/install

# Rebuild xushi with raylib prefix
cmake -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=<path-to>/raylib-5.5/install
cmake --build build --config Release
build\Release\xushi_viewer.exe scenarios\default.replay
```

### Controls

| Key | Action |
|-----|--------|
| Space | Play / pause |
| Left / Right | Step backward / forward (when paused) |
| + / - | Increase / decrease playback speed |
| Scroll wheel | Zoom in / out |
| Right-click drag | Pan camera |
| R | Toggle sensor range overlay |
| W | Toggle waypoint path overlay |
| D | Toggle designation overlay |
| Timeline bar | Click / drag to scrub |

## Testing

17 test files using a custom test harness (no external framework), all run via CTest.

```bash
ctest --test-dir build --output-on-failure
```

| Test | Covers |
|------|--------|
| `test_los` | Line-of-sight raycasting through obstacles |
| `test_rng` | splitmix64 determinism and distribution |
| `test_sensing` | Detection with range, noise, miss rate |
| `test_comm` | Message latency, loss, delivery |
| `test_belief` | Track update, decay, expiration, negative evidence |
| `test_json` | JSON parser correctness |
| `test_scenario` | Scenario loading and validation |
| `test_replay` | NDJSON replay write/read round-trip |
| `test_replay_path_resolution` | Replay file path derivation |
| `test_movement` | Waypoint navigation, loop/stop, branching |
| `test_determinism` | Same seed → identical world hashes |
| `test_golden` | Golden-file comparison for known outputs |
| `test_parity` | Simulation parity checks |
| `test_policy` | Patrol policy waypoint cycling |
| `test_action` | Action request adjudication, designation lifecycle |
| `test_engagement` | Engagement gate evaluation |

## CI

GitHub Actions runs on every push and PR:

1. **build-and-test** — Ubuntu (GCC + Clang) and macOS (Apple Clang), Debug + Release, all tests
2. **determinism-replay-diff** — runs 4 scenarios twice, diffs replay files byte-for-byte
3. **lint** — clang-tidy + cppcheck on all source
4. **scenario-validation** — validates JSON syntax and loads all scenarios
5. **viewer-build** — builds raylib from source, verifies `xushi_viewer` compiles
