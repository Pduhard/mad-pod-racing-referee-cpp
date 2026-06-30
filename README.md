# mad-pod-racing-referee-cpp

A **byte-exact C++ referee and physics engine** for CodinGame's
[Mad Pod Racing](https://www.codingame.com/multiplayer/bot-programming/mad-pod-racing)
(a.k.a. *Coders Strike Back*).

There's no official open-source referee for this game, and the good community
ones are scarce or in other languages (e.g.
[robostac's Go referee](https://github.com/robostac/coders-strike-back-referee)).
This project aims to be a clean, fast, **reusable** C++ engine that:

- simulates the game **identically to CodinGame's server** (held to byte parity
  with a real CG replay — see below);
- doubles as a drop-in **referee** for local tournament tooling
  ([cg-colosseum](https://github.com/Pduhard/cg-colosseum) / the Brutaltester
  protocol);
- exposes a header-only physics core (`engine.h`) you can reuse in **bots**,
  **search-based AIs**, and **reinforcement-learning environments**.

> **Status: working and validated.** The engine and referee are implemented and
> unit-tested, and all six CodinGame leagues (Wood 2 → Legend) are supported via
> the `-d "league="` flag. The physics is held to byte parity with a real CG
> replay — run `make validate`.

## Why "byte-exact" matters

If your engine doesn't reproduce the server's exact arithmetic (integer
truncation of velocity, rotation caps, collision impulse, rounding order), a
bot that looks great locally diverges online. This engine is held to **byte
parity with real CodinGame replays**, so what you simulate is what you get.

## Validation

Correctness is checked three ways:

1. **State parity vs a real CG replay** — `make validate`. A recorded CG game
   ([`validate/fixture_870433349.txt`](validate/fixture_870433349.txt)) is
   replayed turn-by-turn through `engine.h`; every pod's position, velocity, and
   heading must match the recording exactly. Grow the corpus by dropping more
   fixtures in `validate/` (extracted via
   [`validate/extract_replay.py`](validate/extract_replay.py)).
2. **Byte-exact bot I/O vs a replay corpus** via
   [`cg-colosseum validate-referee`](https://github.com/Pduhard/cg-colosseum):
   recorded games are replayed through this referee and the per-turn bytes it
   sends bots must match the server's exactly.
3. **Cross-checked** against [robostac's referee](https://github.com/robostac/coders-strike-back-referee)
   (Go, MIT), which is itself validated against 500+ replays. Physics constants
   and simulation order here were confirmed against it.

## Leagues

CodinGame gates this game across six leagues. **The physics is identical across
all leagues** — each one only unlocks a mechanic (and Gold also adds a 2nd pod
and the raw protocol). The referee mirrors CodinGame's `leagueLevel` with a
single flag, `-d "league=<name>"` (default `silver`):

| `league=` | Pods/player | Protocol | Boost | Shield | Collisions |
|-----------|:-----------:|----------|:-----:|:------:|:----------:|
| `wood2`   | 1 | pre-computed | — | — | — |
| `wood1`   | 1 | pre-computed | ✅ | — | — |
| `bronze`  | 1 | pre-computed | ✅ | — | ✅ |
| `silver`  | 1 | pre-computed | ✅ | ✅ | ✅ |
| `gold`    | 2 | raw | ✅ | ✅ | ✅ |
| `legend`  | 2 | raw (identical to Gold) | ✅ | ✅ | ✅ |

```
./csb-referee -p1 "<bot1>" -p2 "<bot2>" -d "seed=N" -d "league=gold"
```

See [`GAME_SPECIFICATION.md`](GAME_SPECIFICATION.md) for both protocols.

## Layout

```
GAME_SPECIFICATION.md   the byte-exact contract (rules, constants, protocols)
src/engine.h            header-only physics core (reusable)
src/referee.cpp         cg-colosseum / Brutaltester referee (-p1 -p2 -d seed -d league)
src/test.cpp            unit + parity tests
Makefile                build (referee, tests, bench)
```

## Credits

- Physics reference & cross-check: [robostac/coders-strike-back-referee](https://github.com/robostac/coders-strike-back-referee) (MIT).
- Validation harness: [cg-colosseum](https://github.com/Pduhard/cg-colosseum).

## License

[MIT](LICENSE).
