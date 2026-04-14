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

### Design test for every non-shoot action

> Does it **create certainty**, **deny certainty**, or **weaponize false certainty**?

If an action does not pass this test, it does not belong in the information or cyber layer.
Good intel should be mechanically rewarding to act on, not just useful to have.

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

Teams resolve support actions **in initiative order** (the team with initiative this round goes first).
Initiative alternates each round (attacker has initiative on round 1).
Effects are immediate — the first team's jam affects the second team's support actions.

Each team spends from its **support AP pool** (2 SAP).
Available support actions:

| Action           | Cost  | Effect                                               |
|------------------|-------|------------------------------------------------------|
| Drone move       | 1 SAP | Move drone to a target cell within 3 cells (engine picks shortest legal path) |
| Drone scan       | 1 SAP | Verify: all enemies in drone vision get FRESH tracks with uncertainty 0 and confirmed class_id. Clears any spoofed tracks in drone vision. |
| Cyber: jam       | 1 SAP | Degrade comms in 3-cell radius for 1 round           |
| Cyber: lock door | 1 SAP | Lock a door within cyber range (3 cells of friendly) |
| Cyber: unlock    | 1 SAP | Unlock a locked door within cyber range              |
| Cyber: cut lights| 1 SAP | Reduce vision range by 3 in target room; LOS contacts in dark room produce STALE tracks (not FRESH). 2 rounds. |
| Cyber: spoof     | 1 SAP | Inject false track into enemy team belief             |
| Cyber: take cam  | 1 SAP | Gain feed from a camera within cyber range            |

After both teams resolve support:
- All drone feeds update the owning team's belief (unless jammed).
- All controlled camera feeds update the owning team's belief.
- Comms-linked sightings propagate to shared team belief.

### Phase 3 — Activation Phase

Teams **alternate** activating one operator at a time.
The team with initiative activates first (attacker on round 1, then alternates).

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

| Condition                  | Modifier | Rationale                                      |
|----------------------------|----------|-------------------------------------------------|
| FRESH track on target      | +10%     | Good intel pays off — you know exactly where they are |
| Target in COVER            | -25%     | Cover is the primary defensive mechanic         |
| STALE track on target      | -20%     | Acting on old info is risky                     |
| Overwatch snap fire        | -15%     | Reaction shots are hurried                      |
| Target moved this round    | -10%     | Moving targets are harder to hit                |

Modifiers stack additively. Minimum hit probability: 5%. Maximum: 95%.

The FRESH bonus and STALE penalty make information work feel mechanically rewarding.
A drone scan that refreshes a track to FRESH directly improves the next shot by 30 percentage points
compared to shooting at a STALE target (from -20% to +10%).

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
- Drones can: move, observe (passive — creates tracks at normal quality), scan (active — 1 SAP, see below).
- **Passive observation**: drone vision updates team belief during support phase resolution. Tracks from passive observation behave normally (FRESH, decay to STALE, etc.).
- **Active scan**: costs 1 SAP. Qualitatively different from passive observation:
  - All enemies in drone vision get tracks upgraded to FRESH with uncertainty 0 and confirmed class_id.
  - Any spoofed tracks in drone vision are identified and removed from team belief.
  - This is the "verify" action — it creates high-confidence intel and cleans disinformation.
  - Scan makes the drone worth its cost: passive observation tells you something is there, scan tells you exactly what and where, and clears traps.
- Battery decrements by 1 at start of each round (after deployment). At 0 battery, drone is grounded (cannot move, still reports vision from current cell until destroyed).
- **Noise**: drone movement creates a noise event (see Section 8, Noise Events). Enemies within 2 cells who lack LOS to the drone get a STALE track with uncertainty 2 cells. Enemies with LOS get a normal precise track.
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

Every cyber action must pass the design test: create, deny, or weaponize certainty.

| Action      | Target       | Certainty role     | Effect                                                          | Duration   |
|-------------|-------------|--------------------|-----------------------------------------------------------------|------------|
| Jam         | Cell         | **Denies**         | All units within 3-cell radius have degraded comms: tracks from jammed units are NOT shared. | 1 round    |
| Lock door   | Door         | **Denies** (future)| Door becomes LOCKED. Denies future sight lines and movement until breached. | Permanent until unlocked |
| Unlock door | Door         | **Creates** (future)| LOCKED door becomes CLOSED (can now be opened normally).       | Instant    |
| Cut lights  | Room         | **Denies**         | All units in target room have vision range reduced by 3 cells. Direct LOS contacts from darkened units produce STALE tracks instead of FRESH. | 2 rounds   |
| Spoof       | Cell         | **Weaponizes**     | Inject a false track into enemy team belief at target cell. Confidence 0.6, class_id 0 (unknown). | Decays normally |
| Take camera | Camera       | **Creates**        | Transfer camera ownership to your team. Camera feeds now update your belief. | Until re-taken |
| Scan network| Adjacent device | **Creates**     | Reveal all devices connected to the same room or within 5 cells.| Instant    |

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

### Noise events

Loud actions create information for the opposing team even without direct LOS.
Noise events create tracks in the opposing team's belief.

| Source          | Hearing range | Track quality                                              |
|-----------------|---------------|------------------------------------------------------------|
| Breach          | 6 cells       | STALE track of the breacher, uncertainty 3 cells, centered on door |
| Gunshot         | 8 cells       | STALE track of the shooter, uncertainty 2 cells, centered on shooter's cell |
| Drone movement  | 2 cells       | STALE track of the drone, uncertainty 2 cells              |

Rules:
- Noise creates tracks only for enemies **within hearing range** who do **not** already have LOS to the source. (If they have LOS, they already have a better track.)
- Noise tracks are always STALE (not FRESH). They tell you "something happened over there" but not exactly where.
- Noise travels through walls and closed doors (sound carries). It does NOT create LOS.
- Noise tracks decay normally.
- **Quiet actions** (open/close door, peek, move, interact) do not create noise events.

Noise makes loud actions a real information tradeoff: breaching gets you through a door but tells the enemy roughly where you are. Suppressing with gunfire reveals your position. The drone's hum gives away its sector.

### Sharing rules

- **Direct LOS contact** by an operator: immediate team belief update (if operator is not in a jammed zone).
- **Drone feed**: team belief updates during support phase (if drone is not jammed).
- **Camera feed**: team belief updates during support phase (if camera is controlled by team).
- **Noise events**: opposing team gets STALE tracks as described above (noise is not affected by jamming — it's physical, not electronic).
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

### Observation format

Belief is **rasterized into grid channels** (W x H each) rather than exposed as variable-length track lists.
This is easier for conv-style policies and produces cleaner debugging overlays.

#### Grid channels (W x H float maps)

```
Static map (constant after load):
  wall_map              1.0 where WALL, else 0.0
  cover_map             1.0 where COVER, else 0.0
  door_map              1.0 on cells adjacent to doors, else 0.0

Dynamic map state:
  door_state_map        per-door-edge: 0.0 open, 0.5 closed, 1.0 locked (projected onto adjacent cells)
  light_map             0.0 where lights are on, 1.0 where cut (per-room)
  jam_map               1.0 where jammed, else 0.0
  camera_ownership_map  -1.0 enemy, 0.0 neutral/none, 1.0 friendly (at camera cells)

Friendly state:
  friendly_position_map 1.0 at each friendly operator position
  friendly_hp_map       normalized HP (0.0-1.0) at each friendly position
  friendly_overwatch    1.0 at each friendly operator with active overwatch
  drone_position_map    1.0 at friendly drone position (0 everywhere if undeployed/destroyed)

Enemy belief (from team belief, NOT truth):
  enemy_confidence_map  track confidence (0.0-1.0) at estimated position; 0 if no track
  enemy_uncertainty_map track uncertainty radius (normalized) at estimated position
  enemy_freshness_map   1.0 for FRESH, 0.5 for STALE, 0.0 if no track
  explored_map          1.0 for cells this team has observed, else 0.0
```

#### Scalar features (per activation)

```
  round_number          int (normalized to [0,1] by max_rounds)
  rounds_remaining      int (normalized)
  team_support_ap       int
  active_unit_hp        float (normalized)
  active_unit_ap        int
  active_unit_ammo      int (normalized)
  active_unit_has_shot  bool
  drone_deployed        bool
  drone_battery         int (normalized)
  drone_jammed          bool
  initiative_this_round bool (does our team have initiative?)
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
  DRONE_MOVE(target)    target cell within 3 cells; engine picks shortest legal path
  DRONE_SCAN            verify: FRESH + class confirm + clear spoofs in drone vision
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
- Sound propagation beyond defined noise events (breach, gunshot, drone movement)
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
