# Migration status

Tracks the transition from the old continuous-tick sensor/engagement engine
to the turn-based tactical contract engine specified in
[`turn_based_tactical_contract.md`](../turn_based_tactical_contract.md).

## Step 1 — Grid foundation + continuous-sim purge (DONE)

### Landed

- `src/grid.{h,cpp}` — `GridPos`, `CellType` (FLOOR/WALL/COVER), edge-based
  `DoorEdge` (OPEN/CLOSED/LOCKED), Bresenham LOS (COVER does not block,
  diagonal corner-cutting protected), BFS room detection, 8-connected
  neighbors honoring door states.
- `src/comm.{h,cpp}` — binary jam model (cell-and-radius, round duration).
- `src/belief.{h,cpp}`, `src/belief_state.h` — `Sighting`-driven track
  updates on `GridPos`, per-round decay, spoof clearing for drone scans.
- `src/scenario.{h,cpp}` — grid-native scenario JSON (ASCII rows + door
  list + device list + team forces + belief tuning + game mode).
- `src/sim_engine.{h,cpp}` — `round_start → support → activations →
  round_end` phase driver with AP and support-AP ledgers, initiative
  alternation, activation order (initiative team first), deterministic
  world-hash emission.
- `src/world_hash.{h,cpp}` — integer-only FNV-1a over `GridPos`, HP, AP,
  team, and track fields; no floating-point determinism risk.
- `src/game_mode.{h,cpp}`, `src/asset_protection_mode.*`,
  `src/office_breach_mode.*` — clean interface with factory; two modes
  carrying the contract's win conditions.
- `src/replay.{h,cpp}`, `src/replay_events.h` — NDJSON framework retained;
  events re-pointed at new schema (round numbering, GridPos payloads,
  game_mode_end).
- `src/main.cpp` — grid-native CLI (`xushi [--quiet] scenario.json`) that
  runs rounds and writes a deterministic replay.
- `scenarios/small_office_breach.json` — 16×12 grid, 2+2 operators, 1+1
  drones, camera/terminal/light devices, one CLOSED objective-room door.
- `tests/test_grid.cpp` — new: Bresenham LOS cases, COVER-transparent LOS,
  LOS symmetry sweep, door-blocked LOS, room separation/unification by
  door state, 8-neighbors respect walls and doors, bounds.
- Rewritten: `test_belief`, `test_comm`, `test_replay`, `test_scenario`,
  `test_contract`, `test_game_mode`. `test_rng` and `test_json` unchanged.
- `CMakeLists.txt` updated to the new source set.

### Deleted (contract §15 replacement / §13 out-of-scope)

Source:

- `src/sensing.*`, `src/engagement.*` (14-gate engagement)
- `src/map.*` (AABB obstacle map)
- `src/movement.h` (velocity / waypoint nav)
- `src/patrol_policy.h`, `src/policy.h` (no policies in v1)
- `src/task.h` (no VERIFY automation in v1)
- `src/action.h` (old action-request adjudication; new action system is
  deferred to Step 2)
- `src/observation_state.h`, `src/truth_state.h` (old perception/truth
  representations)
- `src/stats.*` (tied to deleted systems)
- `src/path_resolve.*` (no consumers after sensing removal)
- `src/sim.h`, `src/sim.cpp` (obsolete wrapper)

Tests:

- `tests/test_sensing`, `test_movement`, `test_engagement`, `test_action`,
  `test_parity`, `test_replay_path_resolution`, `test_los` (replaced by
  `test_grid`)

Scenarios: every old continuous-space scenario under `scenarios/` has been
removed. Only `small_office_breach.json` remains.

### Determinism

- Two runs of `./build/xushi scenarios/small_office_breach.json --quiet`
  produce byte-identical `.replay` output.
- All world-hash inputs are integer.

## Next steps

Ordered, each step closes a vertical slice:

1. **Operator actions + hit probability.** Implement `ActionRequest` with
   Move / Shoot / Overwatch / Peek / OpenDoor / CloseDoor / Breach /
   DeployDrone / Interact / HackDevice. Wire the contract's §4 hit
   probability formula (FRESH +10, COVER −25, STALE −20, overwatch −15,
   moved −10, clamped to [5%, 95%]). Apply damage and AP costs. Activation
   phase actually does something, not a no-op.
2. **Drone entity + basic support phase.** Battery, noise event, comm
   link, active scan (FRESH + class confirm + spoof clear), drone-move via
   support AP.
3. **Overwatch / reaction interrupts.** Movement triggers set overwatch
   zones, multi-overwatch crossfire resolution.
4. **Cyber action layer.** Jam (already wired at the `CommSystem` level),
   lock/unlock doors, cut lights, spoof, take-camera, scan-net.
5. **Noise events.** Breach / gunshot / drone-move → STALE tracks in the
   opposing team's belief.
6. **Win condition plumbing.** `office_breach_mode::notify_objective_interacted`
   driven from operator INTERACT action on the objective cell.
7. **RL observation rasterization.** §11 grid channels + per-activation
   scalar features.
8. **Viewer update.** Grid + doors + team-belief overlay.

## Not in v1 (contract §13)

Do not add to any step: facing, directional cover, reload, sidearms,
grenades, smoke, multi-round hack timers, relay topology, comm latency,
drone hijacking, turrets, elevation, destructible terrain, sound
propagation beyond the three defined noise events, >2 teams, asymmetric
AP, equipment selection, campaign persistence.
