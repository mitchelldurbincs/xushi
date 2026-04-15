# xushi 虚实

Deterministic, turn-based tactical shooter on a discrete grid. Squads of
operators fight with rifles, drones, and cyber actions across an
information-rich physical map.

> 虚实 (xūshí) — "the empty and the full," from Sun Tzu. Agents act on a
> team belief built from noisy sensors, decaying tracks, and adversarial
> cyber — while truth state stays hidden.

The authoritative specification is
[`turn_based_tactical_contract.md`](./turn_based_tactical_contract.md). If
the code and the contract disagree, the contract wins until amended.

## Status

Migration from a previous continuous-tick sensor/engagement simulator is in
progress. What lands in the current tree:

- Grid primitives (`src/grid.{h,cpp}`): typed cells (FLOOR/WALL/COVER),
  edge-based doors (OPEN/CLOSED/LOCKED), Bresenham LOS, 8-connected
  movement, BFS room detection.
- Round / phase driver (`src/sim_engine.{h,cpp}`): deterministic
  round_start → support → activations → round_end loop with AP and
  support-AP ledgers, initiative alternation, activation ordering.
- Team belief state with track lifecycle (FRESH → STALE → EXPIRED),
  per-round decay, spoof injection & drone-scan clearing.
- Binary jam comm model (`src/comm.{h,cpp}`): cell-and-radius jams with
  round durations.
- Deterministic replay as NDJSON + FNV-1a integer-only world hash.
- Game modes: `asset_protection` and `office_breach` (win conditions,
  timeout tiebreakers).
- First scenario: `scenarios/small_office_breach.json` (16×12, 2+2
  operators, 1+1 drones, camera/terminal/light devices, one door).

What is deliberately **not** in this drop and will land in follow-up steps:
operator actions (move/shoot/overwatch/peek/breach/deploy/interact), drone
control, cyber actions, hit probability resolution, noise events, RL
observation rasterization, and a grid-aware viewer. See
`docs/migration_status.md`.

## Build & run

Requirements: C++17 compiler, CMake 3.20+.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Run the scenario (writes scenarios/small_office_breach.replay).
./build/xushi scenarios/small_office_breach.json

# Quiet mode (suppresses stdout for CI / batch).
./build/xushi --quiet scenarios/small_office_breach.json
```

## Tests

Custom test harness under CTest. 9 suites, all green:

```bash
ctest --test-dir build --output-on-failure
```

| Test      | Covers                                                    |
|-----------|-----------------------------------------------------------|
| rng       | splitmix64 determinism                                    |
| json      | Zero-dep JSON parser                                      |
| grid      | Bresenham LOS, cover transparency, door edges, rooms, 8-neighbors |
| comm      | Binary jam add / tick-down / coverage                     |
| belief    | Sighting ingest, FRESH→STALE→EXPIRED, spoof clearing      |
| replay    | NDJSON round-trip, event schema                           |
| scenario  | JSON loading, validation, `small_office_breach.json`      |
| contract  | Round phase order, initiative alternation, AP refresh, determinism |
| game_mode | Factory, asset_protection timeout, office_breach win conds |

## Scenario format (v1)

A scenario is a JSON file with the following top-level fields:

| Field       | Type     | Notes                                                       |
|-------------|----------|-------------------------------------------------------------|
| `seed`      | int      | splitmix64 seed                                             |
| `rounds`    | int      | Number of rounds (default 12)                               |
| `map.rows`  | string[] | ASCII grid. Chars: `.` FLOOR, `W` WALL, `C` COVER           |
| `map.doors` | array    | `{a, b, state}` with `a`, `b` adjacent cells, state open/closed/locked |
| `entities`  | array    | Operators and drones with `id`, `kind`, `pos`, `team`, etc. |
| `devices`   | array    | Cameras, relays, terminals, light switches                  |
| `belief`    | object   | Track tuning — `fresh_rounds`, `stale_rounds`, etc.         |
| `game_mode` | object   | Optional — `type: "office_breach"` or `"asset_protection"`  |

See `scenarios/small_office_breach.json` for a complete example.

## Source layout

```
src/
  grid.{h,cpp}           GridPos, CellType, DoorEdge, GridMap, LOS, rooms
  sim_engine.{h,cpp}     Round/phase driver, AP ledgers, activation order
  scenario.{h,cpp}       Grid-based scenario JSON loader
  belief.{h,cpp}         Track, Sighting, FRESH/STALE/EXPIRED lifecycle
  belief_state.h         Per-team belief store
  comm.{h,cpp}           Binary jam model
  game_mode.{h,cpp}      Interface + factory
  asset_protection_mode.{h,cpp}
  office_breach_mode.{h,cpp}
  replay.{h,cpp}         NDJSON replay writer/reader
  replay_events.h        Event factories for round_start, round_end, track_update, world_hash, ...
  world_hash.{h,cpp}     Canonical FNV-1a (integer-only inputs)
  json.{h,cpp}           Zero-dep JSON parser
  rng.h                  splitmix64
  types.h                EntityId + grid.h include
  constants.h            Contract-defined constants
  invariants.h           Debug invariant assertions
  main.cpp               CLI entry point
```

The previous continuous-tick engine (sensing, engagement gates, AABB
obstacles, message latency, VERIFY tasks, waypoint movement) has been
removed in this migration step.
