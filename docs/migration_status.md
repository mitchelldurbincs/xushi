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

## Step 2 — Operator actions + hit probability (DONE)

### Landed

- `src/action.h` — `ActionKind` enum + `ActionRequest` plain struct with
  static factories (`move`, `shoot`, `overwatch`, …, `end_turn`). No
  virtuals, no policy objects (deliberate — see §15 of the contract: the
  old policy layer was deleted and is not coming back).
- `src/sim_engine.{h,cpp}` — `step_activation` now takes an
  `ActionRequest`; companion `activation_needs_action()` drives the loop.
  Real action dispatcher with per-action helpers
  (`apply_move`, `resolve_shot`, `set_overwatch`, `apply_door_action`,
  `apply_deploy_drone`). Skip-if-dead at the top of `step_activation`
  handles mid-round elimination (contract §10).
- `compute_hit_probability` (public for unit tests) implements §4 exactly:
  base 70%, FRESH +10, STALE −20, COVER −25 (operators only — drones over
  COVER get no bonus per §5), overwatch snap −15 (used in Step 3 when
  triggers wire up), moved-this-round −10. Clamped to [5%, 95%].
- RNG draw discipline: exactly one `rng_.uniform()` per *resolved* shot
  (post all validation). Documented in `resolve_shot`. This is the only
  Step 2 RNG consumer; preserves §14 byte-identical replay.
- `RoundContext` extended with `moved_this_round` (set<EntityId>).
  Per-activation scratch `ActivationState` (`actor`, `started`,
  `shot_this_activation`, `ended_turn`) lives on `SimEngine` and is reset
  when the cursor advances.
- `ScenarioEntity` gained `overwatch_active`. `world_hash` now mixes
  `overwatch_active` and `drone_deployed` so divergences in those flags
  are detected.
- `RoundHooks` extended with `on_unit_moved`, `on_shot_resolved`,
  `on_damage`, `on_overwatch_set`, `on_door_state_changed`. Default
  no-ops; `CliHooks` in `main.cpp` logs them all to the replay.
- `replay_events.h` gained `replay_unit_moved`, `replay_shot_resolved`,
  `replay_damage`, `replay_overwatch_set`, `replay_door_state`. The
  shot-resolved event carries the full per-modifier breakdown as
  *integer percent points* (`lround(p * 100)`), keeping float values out
  of any deterministic hash input per §14.
- `tests/test_action.cpp` — 17 cases covering move + AP/pos, blocked move
  no-op, shoot hit/miss/AP/ammo accounting, 1-shot-per-activation guard,
  hit-probability formula (cover, fresh+cover, floor/ceiling clamps),
  shoot kills + eliminated-from-next-round, post-shot belief refresh,
  overwatch sets flag + spends 2 AP, overwatch rejected after prior shot,
  open/breach door, same-seed determinism, and different-seed divergence
  on a shot round (closes the latent gap noted in `test_contract.cpp`).
- All 10 test suites green; `./build/xushi scenarios/small_office_breach.json`
  produces byte-identical replay across two consecutive runs.

### Deferred (intentional, per migration order)

- **Noise events** (§8): gunshot/breach STALE-track injection in opposing
  team belief is Step 5. Step 2 only mutates the shooter's *own* team
  belief from the LOS sighting that gates the shot.
- **Overwatch triggers** (§7): Step 2 only *sets* the overwatch flag and
  spends 2 AP. The trigger-on-enemy-movement, snap-shot resolution, and
  multi-overwatch crossfire ordering land in Step 3.
- **Win-condition wiring from Interact** (§10): Interact spends AP but
  does not yet flip `OfficeBreachMode::objective_done_`. Step 6.
- **HackDevice cyber effects** (§6): the action spends AP but is otherwise
  inert. Step 4.

## Next steps

Ordered, each step closes a vertical slice:

3. **Overwatch / reaction interrupts.** Movement triggers set overwatch
   zones, multi-overwatch crossfire resolution.
4. **Drone entity + basic support phase.** Battery, noise event, comm
   link, active scan (FRESH + class confirm + spoof clear), drone-move via
   support AP.
5. **Cyber action layer.** Jam (already wired at the `CommSystem` level),
   lock/unlock doors, cut lights, spoof, take-camera, scan-net.
6. **Noise events.** Breach / gunshot / drone-move → STALE tracks in the
   opposing team's belief.
7. **Win condition plumbing.** `office_breach_mode::notify_objective_interacted`
   driven from operator INTERACT action on the objective cell.
8. **RL observation rasterization.** §11 grid channels + per-activation
   scalar features.
9. **Viewer update.** Grid + doors + team-belief overlay.

## Not in v1 (contract §13)

Do not add to any step: facing, directional cover, reload, sidearms,
grenades, smoke, multi-round hack timers, relay topology, comm latency,
drone hijacking, turrets, elevation, destructible terrain, sound
propagation beyond the three defined noise events, >2 teams, asymmetric
AP, equipment selection, campaign persistence.
