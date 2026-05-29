#pragma once
// engine.h — byte-exact Mad Pod Racing / Coders Strike Back physics core.
//
// Header-only and league-agnostic: the physics is identical across all CG
// leagues; only the I/O protocol and pod count differ (see GAME_SPECIFICATION.md).
// Reusable by the referee, by bots, and by RL environments.
//
// Constants and arithmetic are cross-checked against
// robostac/coders-strike-back-referee (Go, MIT).
#include <cmath>

namespace engine {

constexpr double FRICTION = 0.85;
constexpr double MAX_ROTATE = 18.0 * M_PI / 180.0;  // 18 deg/turn cap

struct Pod {
    double x = 0;      // position
    double y = 0;
    double vx = 0;     // velocity
    double vy = 0;
    double angle = 0;  // facing, radians (0 == +x)
};

struct Command {
    double targetX = 0;
    double targetY = 0;
    int thrust = 0;
};

// Advance a single pod one turn (no rotation/collision yet): thrust along the
// current facing, move by the new velocity, apply friction, round position.
inline void simulateTurn(Pod& pod, const Command& cmd) {
    // Apply thrust along the current facing.
    pod.vx += std::cos(pod.angle) * cmd.thrust;
    pod.vy += std::sin(pod.angle) * cmd.thrust;

    // Move (full turn, no collision).
    pod.x += pod.vx;
    pod.y += pod.vy;

    // End of turn: friction truncates velocity toward zero, position rounds.
    pod.vx = std::trunc(pod.vx * FRICTION);
    pod.vy = std::trunc(pod.vy * FRICTION);
    pod.x = std::round(pod.x);
    pod.y = std::round(pod.y);
}

}  // namespace engine
