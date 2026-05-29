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
constexpr double TWO_PI = 2.0 * M_PI;
constexpr double MAX_ROTATE = 18.0 * M_PI / 180.0;  // 18 deg/turn cap

// CG/robostac rounding: floor(x + 0.5), HALF_UP toward +inf. NOT std::round
// (half-away-from-zero) — they differ on negative halves, e.g. -2.5.
inline double roundHalfUp(double x) { return std::floor(x + 0.5); }

// Absolute heading from (ax,ay) to (bx,by), radians.
inline double getAngle(double ax, double ay, double bx, double by) {
    return std::atan2(by - ay, bx - ax);
}

inline double normalizeAngle(double a) {
    while (a < 0) a += TWO_PI;
    while (a > TWO_PI) a -= TWO_PI;
    return a;
}

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

// Signed shortest rotation (radians) from the pod's facing toward (tx,ty).
inline double diffAngle(const Pod& pod, double tx, double ty) {
    double a = getAngle(pod.x, pod.y, tx, ty);
    double da = std::fmod(a - pod.angle, TWO_PI);
    return std::fmod(2 * da, TWO_PI) - da;
}

// Rotate toward the target, capped at MAX_ROTATE per turn. Within the cap the
// pod snaps directly to the target heading. No angle normalization here — this
// mirrors robostac's applyRotate (normalization is only done on the first turn).
inline void rotateToward(Pod& pod, double tx, double ty) {
    double a = getAngle(pod.x, pod.y, tx, ty);
    double rot = diffAngle(pod, tx, ty);
    if (rot < -MAX_ROTATE) a = pod.angle - MAX_ROTATE;
    if (rot > MAX_ROTATE) a = pod.angle + MAX_ROTATE;
    pod.angle = a;
}

// Advance a single pod one turn (no collision yet): rotate toward target,
// thrust along the new facing, move, apply friction, round position. On the
// first turn the pod faces its target instantly (no rotation cap).
inline void simulateTurn(Pod& pod, const Command& cmd, bool firstTurn = false) {
    if (firstTurn) {
        pod.angle = normalizeAngle(getAngle(pod.x, pod.y, cmd.targetX, cmd.targetY));
    } else {
        rotateToward(pod, cmd.targetX, cmd.targetY);
    }

    pod.vx += std::cos(pod.angle) * cmd.thrust;
    pod.vy += std::sin(pod.angle) * cmd.thrust;

    pod.x += pod.vx;
    pod.y += pod.vy;

    pod.vx = std::trunc(pod.vx * FRICTION);
    pod.vy = std::trunc(pod.vy * FRICTION);
    pod.x = roundHalfUp(pod.x);
    pod.y = roundHalfUp(pod.y);
}

}  // namespace engine
