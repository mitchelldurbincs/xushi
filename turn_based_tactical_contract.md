# Turn-Based Tactical Contract v1

This document is the authoritative reference for xushi v1.
Every rule here can become a test assertion.
If the spec and the code disagree, the spec wins until the spec is amended.

---

## 1. Identity

A deterministic, turn-based tactical shooter on a discrete grid.
Squads fight with operators, drones, and cyber actions.
Bullets decide firefights. Drones and cyber decide who fights on the better picture.

Three intertwined layers:
- **Physical**: operators move, shoot, breach, take cover, overwatch.
- **Information**: drones, cameras, and direct sighting create tracks, last-known positions, and shared squad awareness.
- **Cyber**: teams manipulate doors, cameras, lights, and comms to reshape the map and the feed.

---

## 2. Round Structure

A game consists of R rounds (scenario-defined, default 12).
Each round has four phases executed in strict order.

### Phase 1 — Start of Round

1. Refresh all operator AP to max (3).
2. Refresh team support AP to max (2 per team).
3. Reset per-operator `shot_this_activation` flag.
4. Decrement timed effects (jam duration, spoof duration, etc.).
5. Remove expired effects.
6. Age all tracks: confidence decays, uncertainty grows (see Section 8).
7. Remove expired tracks.

### Phase 2 — Support Phase

Teams resolve support actions **in team order** (attacker first, then defender).
Effects are immediate — an attacker jam affects the defender's support actions.

Each team spends from its **support AP pool** (2 SAP).
Available support actions:

| Action           | Cost  | Effect                                               |
|------------------|-------|------------------------------------------------------|
| Drone move       | 1 SAP | Move drone up to 3 cells (path must be passable)     |
| Drone scan       | 1 SAP | Drone reports all visible enemies as tracks           |
| Cyber: jam       | 1 SAP | Degrade comms in 3-cell radius for 1 round           |
| Cyber: lock door | 1 SAP | Lock a door within cyber range (3 cells of friendly) |
| Cyber: unlock    | 1 SAP | Unlock a locked door within cyber range              |
| Cyber: cut lights| 1 SAP | Reduce vision range by 3 cells in target room, 2 rounds |
| Cyber: spoof     | 1 SAP | Inject false track into enemy team belief             |
| Cyber: take cam  | 1 SAP | Gain feed from a camera within cyber range            |

After both teams resolve support:
- All drone feeds update the owning team's belief (unless jammed).
- All controlled camera feeds update the owning team's belief.
- Comms-linked sightings propagate to shared team belief.

### Phase 3 — Activation Phase

Teams **alternate** activating one operator at a time.
Attacker activates first on round 1. Initiative alternates each round.

Each activation:
1. Select an unactivated operator.
2. Spend AP on actions (see Section 4).
3. Mark operator as activated this round.

When one team has no unactivated operators remaining, the other team activates all remaining operators **one at a time** sequentially.

**Vision during activations**: when a unit moves into a new cell, compute LOS for:
- The moving unit (what can it now see?).
- All opposing units (can any of them see the mover?).

Any new LOS contact creates or refreshes a track in both teams' beliefs immediately (if comms are not jammed for that unit).

**Overwatch interrupts** trigger during this phase (see Section 7).

### Phase 4 — End of Round

1. Track confidence decays for all stale tracks.
2. Temporary effects tick down.
3. Check win conditions (see Section 10).
4. Emit replay snapshot (deterministic state hash, event log).
5. Advance round counter.

---

## 3. Grid and Map

### Cell types

Each cell is one of:
- **FLOOR**: passable, no cover, does not block LOS.
- **WALL**: impassable, blocks LOS.
- **COVER**: passable, grants cover bonus to occupant, does not block LOS.

### Doors

Doors exist on **edges** between two FLOOR cells.
Door states:
- **OPEN**: passable, does not block LOS.
- **CLOSED**: impassable, blocks LOS.
- **LOCKED**: impassable, blocks LOS. Requires breach (1 AP, loud) or cyber unlock (1 SAP, quiet).

### Adjacency and movement

8-connected grid (cardinal + diagonal). Moving one cell costs 1 AP regardless of direction.
A unit cannot move through WALL cells or CLOSED/LOCKED doors.

### Line of sight

Bresenham line from source cell center to target cell center.
If the line passes through a WALL cell or a CLOSED/LOCKED door edge, LOS is blocked.
COVER cells do NOT block LOS.

LOS is symmetric: if A can see B, B can see A.

### Map objects

Map objects occupy a cell and have state:

| Object    | Properties                                         |
|-----------|-----------------------------------------------------|
| Camera    | team (owner or -1), facing (N/S/E/W), range (cells) |
| Relay     | team (owner or -1), range (cells)                    |
| Terminal  | interaction target for objectives                    |
| Light     | on/off, affects vision range in containing room      |

Cameras provide vision in a cone: 90-degree arc in facing direction, up to range cells, blocked by walls/doors.

### Rooms

A room is a connected region of FLOOR/COVER cells bounded by WALL cells and doors.
Room membership is precomputed at map load for light/effect targeting.

### Map dimensions

First scenario: 16 wide x 12 tall.

---

## 4. Operators

### Stats

| Stat         | Value |
|--------------|-------|
| HP           | 100   |
| Max AP       | 3     |
| Vision range | 10 cells |
| Weapon       | rifle (see below) |
| Ammo         | 10    |

### Rifle

| Stat         | Value |
|--------------|-------|
| Range        | 7 cells |
| Base hit     | 70%   |
| Damage       | 50    |
| Ammo cost    | 1     |

### Hit probability modifiers

| Condition        | Modifier |
|------------------|----------|
| Target in COVER  | -25%     |
| Overwatch snap   | -15%     |
| Target moved this round | -10% |

Modifiers stack additively. Minimum hit probability: 5%. Maximum: 95%.

### Actions

All actions cost AP from the activating operator's pool (3 AP max).

| Action       | AP | Rule                                                         |
|--------------|----|--------------------------------------------------------------|
| Move         | 1  | Move to adjacent passable cell. Triggers LOS checks.         |
| Shoot        | 1  | Fire at visible enemy within range. 1 shot per activation.   |
| Overwatch    | 2  | Set overwatch state (see Section 7). Once per activation.    |
| Peek         | 1  | Reveal all units/objects in an adjacent cell without entering.|
| Open door    | 1  | Open an adjacent unlocked CLOSED door. Quiet.                |
| Close door   | 1  | Close an adjacent OPEN door. Quiet.                          |
| Breach       | 1  | Force open an adjacent CLOSED or LOCKED door. Loud.          |
| Deploy drone | 1  | Place team drone on own cell or adjacent FLOOR cell.          |
| Interact     | 1  | Use adjacent terminal or objective. Context-dependent.        |
| Hack device  | 1  | Interact with adjacent map device (equivalent to cyber action via physical access). |

### Constraints

- **1 shot per activation**: an operator may fire at most once during a single activation.
- **Dead operators** (0 HP) are eliminated: removed from activation order, cannot act.
- **Shooting a drone**: same action as shooting an operator. Drones have 1 HP.

---

## 5. Drones

### Stats

| Stat         | Value |
|--------------|-------|
| HP           | 1     |
| Vision range | 6 cells |
| Speed        | 3 cells per SAP move |
| Battery      | 8 rounds from deployment |
| Noise radius | 2 cells |

### Rules

- Each team has 1 drone. Deployed by an operator (1 AP). Not re-deployable if destroyed.
- Drones are controlled during the **support phase** using support AP.
- Drones cannot: shoot, open doors, interact with objects, carry things.
- Drones can: move, observe (passive — always reports vision), scan (active — 1 SAP).
- Drone vision updates team belief during support phase resolution.
- Battery decrements by 1 at start of each round (after deployment). At 0 battery, drone is grounded (cannot move, still reports vision from current cell until destroyed).
- **Noise**: enemies within noise radius (2 cells) detect the drone's presence (creates a track of the drone, but drone position is uncertain within noise radius).
- **Comm link**: drone feeds require unbroken comms. If the drone is in a jammed zone, its feed does NOT update team belief. The drone still sees locally, but the team doesn't benefit.
- Drones fly over COVER cells but cannot pass through WALL cells or CLOSED doors.

### Counter-drone

- Any operator can shoot a drone during their activation (1 AP, standard hit probability).
- Drones in COVER cells do NOT get cover bonus (they're flying).
- Destroyed drones are removed permanently. Feed ends. Only stale tracks remain.

---

## 6. Cyber

### Core rule

Cyber actions manipulate **map state** and **information state**.
They are spatial, discrete, and visible in replay.

### Cyber range

A cyber action targets a device or location that a friendly unit (operator or drone) can see or is **within 3 cells of**.

### Actions (support phase, cost 1 SAP each)

| Action      | Target       | Effect                                                          | Duration   |
|-------------|-------------|------------------------------------------------------------------|------------|
| Jam         | Cell         | All units within 3-cell radius have degraded comms: tracks from jammed units are NOT shared during next support phase. | 1 round    |
| Lock door   | Door         | Door becomes LOCKED. Requires breach or unlock to open.          | Permanent until unlocked |
| Unlock door | Door         | LOCKED door becomes CLOSED (can now be opened normally).         | Instant    |
| Cut lights  | Room         | All units in target room have vision range reduced by 3 cells.   | 2 rounds   |
| Spoof       | Cell         | Inject a false track into enemy team belief at target cell. Confidence 0.6, class_id 0 (unknown). | Decays normally |
| Take camera | Camera       | Transfer camera ownership to your team. Camera feeds now update your belief. | Until re-taken |
| Scan network| Adjacent device | Reveal all devices connected to the same room or within 5 cells.| Instant    |

### Operator cyber (physical access)

An operator adjacent to a device can spend 1 AP to perform the same effect as a cyber action.
This does NOT cost support AP. It uses the operator's activation AP.
Physical access bypasses cyber range requirements.

---

## 7. Overwatch and Reactions

### Setting overwatch

- Costs 2 AP. Operator must not have fired this activation.
- Creates an overwatch state that persists until:
  - The operator's next activation (next round), OR
  - The overwatch triggers, OR
  - The operator is eliminated.
- Overwatch zone: all cells in the operator's LOS within weapon range.

### Trigger

Overwatch triggers when an **enemy unit** (operator or drone) **moves into or within** the overwatch zone.

Resolution:
1. Enemy movement is **interrupted** at the triggering cell.
2. Overwatching operator takes one snap shot (base hit - 15% overwatch penalty).
3. Overwatch state is consumed (one shot only).
4. If the target survives, it may continue its activation with remaining AP.

### Multiple overwatch

If multiple friendly operators have overwatch covering the same cell, **all** trigger simultaneously when an enemy enters that cell. Each takes one snap shot. Resolve in operator ID order (deterministic).

This enables crossfire setups — a key tactical play.

### Drones and overwatch

Drones entering an overwatch zone trigger overwatch (they can be shot).
Drones do NOT have overwatch capability.

---

## 8. Information Layer

### Three-tier state model

**Truth state** (engine only):
- Exact positions, HP, ammo, AP of all units.
- Exact door states, device ownership, drone positions.
- Exact effect timers, comm graph.
- Never exposed to agents or policies.

**Team belief state** (per team):
- Tracks of known/suspected enemy positions with confidence and uncertainty.
- Known map state (explored cells, door states, device ownership).
- Active effects (jams, light cuts, spoofs).
- Updated during support phase and on direct LOS contact during activations.

**Per-unit observation** (per unit, per activation):
- What this unit can currently see (LOS from current cell).
- Local status (AP, HP, ammo, overwatch state).
- Received team belief updates.

### Track rules

Tracks represent believed enemy positions.

| Field               | Type    | Description                                    |
|---------------------|---------|------------------------------------------------|
| target_id           | uint32  | Entity this track refers to                    |
| estimated_position  | GridPos | Believed cell position                         |
| confidence          | float   | 0.0 to 1.0, how sure we are the enemy is here |
| uncertainty         | float   | Radius of position uncertainty (cells)         |
| last_update_round   | int     | Round this track was last refreshed            |
| status              | enum    | FRESH, STALE, EXPIRED                          |
| class_id            | int     | Believed unit type (0 = unknown)               |
| source_mask         | uint32  | Bitmask of sources that contributed            |

### Track lifecycle

```
Direct LOS / drone scan → FRESH (confidence ~1.0, uncertainty ~0)
      │
      ├── (fresh_rounds elapsed, no refresh) → STALE
      │         confidence decays per round
      │         uncertainty grows per round
      │
      └── (stale_rounds elapsed) → EXPIRED → removed
```

### Track parameters (scenario-configurable)

| Parameter                    | Default |
|------------------------------|---------|
| fresh_rounds                 | 2       |
| stale_rounds                 | 4       |
| uncertainty_growth_per_round | 1.0 cells |
| confidence_decay_per_round   | 0.15    |

### Sharing rules

- **Direct LOS contact** by an operator: immediate team belief update (if operator is not in a jammed zone).
- **Drone feed**: team belief updates during support phase (if drone is not jammed).
- **Camera feed**: team belief updates during support phase (if camera is controlled by team).
- **Jammed unit**: tracks from this unit are NOT shared. The unit has the track locally but it does not propagate to team belief until the jam expires.

---

## 9. Communication

### Base model

Operators on the same team share a radio channel.
Tracks and sightings propagate to team belief automatically, subject to:

1. **Jamming**: if the source unit is within a jammed zone, sharing is blocked for that round.
2. **Drone link**: if a drone is in a jammed zone, its feed does not reach team belief.

### No network topology in v1

V1 does not model relay graphs, signal routing, or propagation delay.
Comms are binary: either shared (unjammed) or blocked (jammed).
Relay nodes exist on the map but have no mechanical effect in v1 (reserved for v2).

### Jam mechanics

- 3-cell radius from target cell.
- Lasts 1 round (expires at start of next round, Phase 1).
- All units within the radius are "jammed":
  - Their direct sightings do NOT propagate to team belief during this round.
  - Drone feeds from within the zone do NOT propagate.
  - The jammed unit still has its own local tracks.
- A unit can be jammed by the enemy team only (you cannot jam yourself).
- Jamming is visible: jammed units know they are jammed. Both teams know a jam is active (the cyber action is visible in replay, and jammed units experience degraded comms).

---

## 10. Win Conditions

### First scenario: office breach

- **Attacker wins** if: any attacker operator occupies the objective terminal cell AND spends 1 AP to interact, OR all defender operators are eliminated.
- **Defender wins** if: all attacker operators are eliminated, OR the round limit is reached without the attacker achieving their objective.
- **Draw**: not possible (defender wins on timeout).

### Elimination

When an operator reaches 0 HP:
1. Operator is marked eliminated.
2. Removed from activation order.
3. Cannot act or be activated.
4. Remains on map as an obstacle (occupies cell but blocks nothing).
5. Existing tracks of the eliminated unit persist and decay normally.

---

## 11. RL Observation Boundary

The policy receives **team belief + per-unit status**. Never truth.

### Observation vector (per activation)

```
Global:
  round_number          int
  team_id               int
  team_support_ap       int
  rounds_remaining      int

Active unit:
  position              GridPos
  hp                    int
  ap_remaining          int
  ammo                  int
  has_shot_this_activ   bool
  overwatch_active      bool
  
Team state:
  for each friendly operator:
    position            GridPos
    hp                  int
    activated_this_rnd  bool
    overwatch_active    bool
  
  drone:
    deployed            bool
    position            GridPos
    battery             int
    jammed              bool

Enemy tracks (from team belief):
  for each track:
    estimated_position  GridPos
    confidence          float
    uncertainty         float
    status              enum {FRESH, STALE}
    class_id            int
    rounds_since_update int

Map knowledge:
  explored_cells        bitfield
  known_door_states     per-door enum
  known_device_owners   per-device team_id
  active_jams           list of (cell, rounds_remaining)
  active_light_cuts     list of (room_id, rounds_remaining)
```

### Action space (per activation)

```
Operator actions:
  MOVE(direction)       8 directions
  SHOOT(target_id)      visible enemy track
  OVERWATCH             set overwatch
  PEEK(direction)       8 directions
  OPEN_DOOR(direction)  adjacent door
  CLOSE_DOOR(direction) adjacent door
  BREACH(direction)     adjacent door
  DEPLOY_DRONE(cell)    own or adjacent cell
  INTERACT              adjacent terminal
  HACK_DEVICE           adjacent device
  END_TURN              pass remaining AP

Support actions (during support phase):
  DRONE_MOVE(path)      up to 3 cells
  DRONE_SCAN            scan from current position
  CYBER_JAM(cell)       target cell
  CYBER_LOCK(door)      target door
  CYBER_UNLOCK(door)    target door
  CYBER_CUT_LIGHTS(room) target room
  CYBER_SPOOF(cell)     target cell
  CYBER_TAKE_CAM(cam)   target camera
  CYBER_SCAN_NET(dev)   adjacent device
  PASS_SUPPORT          spend no more SAP
```

---

## 12. First Scenario — Small Office Breach

### Parameters

| Parameter       | Value |
|-----------------|-------|
| Grid            | 16 x 12 |
| Rounds          | 12    |
| Seed            | scenario-defined |

### Forces

**Attackers (team 0):**
- 2 operators (spawn left side of map)
- 1 drone (undeployed, carried by operator)

**Defenders (team 1):**
- 2 operators (spawn in/near objective room)
- 1 drone (undeployed, carried by operator)

### Map features

- Entry zone (left): open area where attackers spawn.
- Main corridor: connects entry to interior rooms.
- Side rooms (2): accessible via doors, contain COVER cells.
- Back office: contains the objective terminal. Accessible via 2 doors.
- 1 neutral camera: covers main corridor.
- 1 neutral light switch: controls back office lighting.
- 3-4 doors: create chokepoints and breach opportunities.
- COVER cells: scattered in rooms for defensive positions.

### Map layout (conceptual)

```
WWWWWWWWWWWWWWWW
W.....W....W...W
W.A...D..c.W...W
W.....W....D.T.W
W..WWWWWWW.W...W
W..........W...W
W..........D...W
W..WWWWWWW.W...W
W.....W....D...W
W.A...D..C.W.L.W
W.....W....W...W
WWWWWWWWWWWWWWWW

Legend:
  W = wall    . = floor    D = door (closed)
  A = attacker spawn       T = terminal (objective)
  C = cover cell           c = camera
  L = light switch
```

The actual cell grid is defined in the scenario JSON.
This ASCII sketch establishes the tactical geometry, not exact cell values.

---

## 13. Not in V1

These are explicitly deferred. Do not implement, stub, or plan for them.

- Facing / directional awareness for operators
- Directional cover (cover bonus depends on attack angle)
- Reload action (ammo is generous enough for 12 rounds)
- Sidearm / weapon switching
- Grenades / throwables
- Smoke / gas / environmental hazards
- Multi-round hack timers
- Relay network topology / signal routing / propagation delay
- Comm latency between operators (comms are instant when unjammed)
- Drone hijacking
- Turrets / automated defenses
- Multiple floors / elevation
- Destructible terrain
- Sound propagation beyond drone noise and breach noise
- Fog of war on the grid (map layout is known; only unit/device states are hidden)
- More than 2 teams
- Asymmetric AP (all operators have same stats)
- Unit loadout / equipment selection
- Campaign / persistent state between scenarios

---

## 14. Determinism and Replay

### Determinism contract

Given the same scenario JSON (including seed), the engine must produce byte-identical replay output.

Requirements:
- All RNG uses a single seeded splitmix64 stream, consumed in fixed order per round.
- All iteration over collections uses deterministic order (sorted by ID, not pointer/hash order).
- No floating-point non-determinism: use consistent rounding, avoid reordering FP operations.
- World hash emitted every round for regression testing.

### Replay format

NDJSON, one event per line. Events include:
- Round start / phase transitions
- Support actions and resolutions
- Operator activations and actions
- Overwatch triggers
- Track creation / update / expiration
- Drone movement / destruction
- Cyber actions and effects
- Damage / elimination events
- World hash
- Game end

Replay must support:
- Forward playback
- Per-round state reconstruction
- Headless batch execution (no viewer required)

---

## 15. Migration from Current Engine

### What carries over

| Current system        | New role                                          |
|-----------------------|---------------------------------------------------|
| Track / BeliefState   | Team belief tracks (confidence, uncertainty, lifecycle) |
| Observation struct    | Simplified to grid-based sighting event           |
| CommSystem concept    | Binary jammed/unjammed check (simplified from latency model) |
| splitmix64 RNG        | Unchanged                                         |
| JSON parser           | Unchanged                                         |
| Replay concept        | Format changes but NDJSON + determinism preserved  |
| World hash            | Unchanged concept, new inputs                     |
| Test harness          | Unchanged                                         |
| Headless-first design | Unchanged                                         |
| TickHooks pattern     | Becomes RoundHooks / PhaseHooks                   |

### What gets replaced

| Current system       | Replacement                                       |
|----------------------|---------------------------------------------------|
| Continuous tick loop | Round-based phase loop                            |
| Vec2 continuous space| GridPos discrete cells                            |
| AABB obstacle map    | Cell grid with typed cells + edge doors           |
| Velocity movement    | AP-based cell-to-cell movement                    |
| Sensing phase        | LOS checks during movement + drone/camera feeds   |
| CommChannel latency  | Binary jammed/unjammed                            |
| ActionType enum      | New action types (move, shoot, overwatch, etc.)   |
| EngagementGates      | Simplified hit probability model                  |
| ScenarioEntity       | Operator / Drone / Device entity types            |
| Scenario JSON format | New format with grid map, doors, devices, forces  |

### New systems (no current equivalent)

- Grid / cell map with room detection
- Door system (edge-based, state machine)
- AP and activation system
- Support AP pool
- Overwatch / reaction system
- Drone entity with battery, noise, link
- Cyber action system
- Camera / light / terminal devices
- Win condition evaluator
