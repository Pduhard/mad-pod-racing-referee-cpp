# mad-pod-racing-referee-cpp

A **byte-exact C++ referee and physics engine** for CodinGame's
[Mad Pod Racing](https://www.codingame.com/multiplayer/bot-programming/mad-pod-racing)
(a.k.a. *Coders Strike Back*).

There's no official open-source referee for this game, and the good community
ones are scarce or in other languages (e.g.
[robostac's Go referee](https://github.com/robostac/coders-strike-back-referee)).
This project aims to be a clean, fast, **reusable** C++ engine that:

- simulates the game **identically to CodinGame's server** (validated byte-exact
  against real CG replays — see below);
- doubles as a drop-in **referee** for local tournament tooling
  ([cg-colosseum](https://github.com/Pduhard/cg-colosseum) / the Brutaltester
  protocol);
- exposes a header-only physics core (`engine.h`) you can reuse in **bots**,
  **search-based AIs**, and **reinforcement-learning environments**.

> **Status: early work in progress.** This commit lands the spec
> ([`GAME_SPECIFICATION.md`](GAME_SPECIFICATION.md)) — the byte-exact contract —
> first. The engine and referee are implemented test-first against it next.

## Why "byte-exact" matters

If your engine doesn't reproduce the server's exact arithmetic (integer
truncation of velocity, rotation caps, collision impulse, rounding order), a
bot that looks great locally diverges online. This engine is held to **byte
parity with real CodinGame replays**, so what you simulate is what you get.

## Validation

Correctness is checked two ways:

1. **Against real CG replays** via
   [`cg-colosseum validate-referee`](https://github.com/Pduhard/cg-colosseum):
   recorded games are replayed through this referee and the per-turn inputs it
   produces must match the server's byte-for-byte.
2. **Cross-checked** against [robostac's referee](https://github.com/robostac/coders-strike-back-referee)
   (Go, MIT), which is itself validated against 500+ replays. Physics constants
   and simulation order here were confirmed against it.

## Leagues

CodinGame gates this game across leagues. **The physics is identical across all
leagues** — only the I/O protocol and pod count change. This engine implements
the full (high-league) simulation; lower-league views are reductions of it. See
[`GAME_SPECIFICATION.md`](GAME_SPECIFICATION.md) for both protocols.

## Layout (planned)

```
GAME_SPECIFICATION.md   the byte-exact contract (rules, constants, protocols)
src/engine.h            header-only physics core (reusable)
src/referee.cpp         cg-colosseum / Brutaltester referee (-p1 -p2 -d seed)
src/test.cpp            unit + parity tests
Makefile                build (referee, tests, bench)
```

## Credits

- Physics reference & cross-check: [robostac/coders-strike-back-referee](https://github.com/robostac/coders-strike-back-referee) (MIT).
- Validation harness: [cg-colosseum](https://github.com/Pduhard/cg-colosseum).

## License

[MIT](LICENSE).
