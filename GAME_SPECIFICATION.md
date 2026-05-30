# Game Specification — Mad Pod Racing / Coders Strike Back

> The single source of truth for this engine. The simulation must reproduce
> CodinGame's server **byte-for-byte**. Constants and ordering below were
> cross-checked against [robostac's referee](https://github.com/robostac/coders-strike-back-referee)
> (Go, MIT, validated vs 500+ CG replays). Anything marked **[pin-in-code]** is a
> precise arithmetic detail to be locked down against real replays via
> `cg-colosseum validate-referee`, not trusted from prose.

## 1. Overview

Two players race **pods** around a circuit of **checkpoints**. To finish a lap, a
pod passes through every checkpoint in order and returns to the start. The first
player to cross the start checkpoint on the final lap wins.

## 2. Map & entities

| Thing | Value |
|---|---|
| Map | 16000 × 9000 units, origin (0,0) top-left |
| Checkpoint radius | **600** (pod center must be within to pass) |
| Pod radius | **400** (collision when center-to-center distance < 800) |
| Pods can leave the map | yes (off-screen but simulated normally) |

## 3. Leagues — physics identical, I/O differs

CodinGame gates the game by league. **The physics (section 5) is the same across
all leagues.** Only the pod count and the I/O protocol change. This engine
implements the full (high-league) simulation; lower leagues are reductions.

| | Low league (Wood/Bronze) | High league (Silver+ / server) |
|---|---|---|
| Pods per player | **1** | **2** |
| Init data | none | `laps` + checkpoint list |
| Per-turn obs | pre-computed: next-CP coords, dist, angle; 1 opponent pos | raw: `x y vx vy angle nextCpId` per pod (yours + opponent's) |
| Output lines | 1 | 2 (one per pod) |

## 4. I/O protocols

### 4a. Bot ↔ game (low league)

Per turn, the bot reads:
```
x y nextCheckpointX nextCheckpointY nextCheckpointDist nextCheckpointAngle   # your pod
opponentX opponentY                                                          # opponent pod
```
`nextCheckpointAngle` ∈ [-180, 180] is the signed angle between the pod's facing
and the direction to the next checkpoint. The bot writes one line:
```
X Y thrust          # thrust ∈ [0,100], or BOOST, or SHIELD
```

### 4b. Bot ↔ game (high league)

Init:
```
laps
checkpointCount
checkpointX checkpointY        # × checkpointCount
```
Per turn (your 2 pods first, then 2 opponent pods):
```
x y vx vy angle nextCheckpointId      # × 4
```
The bot writes 2 lines (one per pod): `X Y thrust|BOOST|SHIELD`.

### 4c. Referee ↔ tournament harness (this project's external interface)

This engine ships a referee binary matching the **cg-colosseum / Brutaltester**
convention:
```
csb-referee -p1 "<bot1 cmd>" -p2 "<bot2 cmd>" -d "seed=N" [-d "key=val" ...]
```
It spawns the bot processes, drives the game, and prints **one integer score per
player** on stdout (one per line), exit 0 on success. Higher score = better.
**[pin-in-code]** exact score formula (robostac uses
`next_checkpoint_index * 1_000_000 − distance_to_next` to rank a non-finishing
pod; a finisher outranks any non-finisher).

## 5. Turn resolution (the byte-exact core)

Each turn, for every pod in order:

1. **Read command** → target `(X, Y)` + power token (`thrust` | `BOOST` | `SHIELD`).
2. **Resolve power:**
   - `SHIELD` → `shieldtimer = 4`; the pod gets **10× mass** for this turn's
     collisions.
   - `BOOST` → thrust **650** on the **first** use of the race (consumes the
     one-shot boost); any later `BOOST` → thrust **100**, the normal max
     (verified byte-exact against a real CG replay; robostac incorrectly uses 200).
   - If `shieldtimer > 0` → **thrust is forced to 0** (engine off).
3. **Rotate** toward the target:
   - **First turn only:** the pod instantly faces its target (no cap). The angle
     is the raw signed `atan2` heading in (-π, π] — NOT normalized to [0, 2π)
     (verified vs real CG; robostac normalizes, which diverges on negative headings).
   - Otherwise: rotate at most **18°** toward the target this turn.
4. **Apply thrust:** `velocity += thrust · (cos angle, sin angle)`.
5. **Move + collisions** (`nextTurn`): advance pods over the turn; resolve
   **elastic** collisions with a **minimum impulse of 120**; a checkpoint is
   passed when the pod center comes within 600 of its next checkpoint (advance
   its `nextCheckpointId`). **[pin-in-code]** exact collision time-stepping
   (earliest-collision ordering within the turn) and tie-breaking.
6. **Friction:** `velocity = trunc(velocity · 0.85)` (truncation toward zero,
   per component). **[pin-in-code]** exact truncation/rounding.
7. **Round position** to integer for output. **[pin-in-code]** rounding rule.
8. **Decrement** `shieldtimer` if > 0.
9. **Timeout:** each pod has a 100-turn counter to reach its next checkpoint;
   reaching it resets the counter; reaching 0 eliminates the player.

## 6. Constants (verified vs robostac)

| Constant | Value | Source |
|---|---|---|
| Friction | `0.85` (velocity truncated after) | `frictionVal` |
| Max rotation / turn | `18°` (first turn exempt) | `maxRotate` |
| Pod radius | `400` (collision dist 800) | `podRSQ = 800²` |
| Checkpoint radius | `600` | `cpRSQ = 600²` |
| Collision min impulse | `120` | `minImpulse` |
| BOOST thrust | `650` once, then `100` (normal max) | real CG replay |
| SHIELD | `shieldtimer = 4`, engine off while > 0 (3 turns), 10× mass | shield handling |
| Normal thrust range | **0–100** (official); robostac rejects `> 200` as invalid | rules + `if v > 200` |
| Per-pod timeout | `100` turns without reaching next checkpoint | `playerTimeout` |
| Pods total | 4 (2 per player), high league | `podCount = 4` |

## 7. Win / loss conditions

- **Win:** first to pass all checkpoints across all laps (cross the start
  checkpoint first on the final lap).
- **Loss:**
  - invalid output (malformed line; thrust out of the accepted range);
  - exceeds the per-turn time limit (first turn ≤ 1000 ms, others ≤ 75 ms);
  - no pod reaches its next checkpoint within 100 turns;
  - the opponent wins.

## 8. Open items to pin during implementation

- Exact collision time-stepping and ordering within a turn **[pin-in-code]**.
- Exact velocity truncation and position rounding **[pin-in-code]**.
- Score formula for ranking non-finishers **[pin-in-code]**.
- Initial pod placement from checkpoints (robostac derives start positions from
  the first checkpoint vector) **[pin-in-code]**.
- Confirm the normal-thrust clamp the **server** applies (docs say 100; robostac
  only rejects > 200) — agents/bots should constrain to **[0, 100]** regardless.

All "[pin-in-code]" items are locked down test-first against real CG replays via
`cg-colosseum validate-referee`, then cross-checked against robostac.
