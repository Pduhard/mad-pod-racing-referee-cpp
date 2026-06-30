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
constexpr double POD_RADIUS = 400.0;
constexpr double POD_RSQ = (2 * POD_RADIUS) * (2 * POD_RADIUS);  // 800^2
constexpr double MIN_IMPULSE = 120.0;
constexpr double BOUNCE_EPS = 0.00001;
constexpr double CP_RADIUS = 600.0;
constexpr double CP_RSQ = CP_RADIUS * CP_RADIUS;  // 600^2

// CG/robostac rounding: floor(x + 0.5), HALF_UP toward +inf. NOT std::round
// (half-away-from-zero) — they differ on negative halves, e.g. -2.5.
inline double roundHalfUp(double x) { return std::floor(x + 0.5); }

// Absolute heading from (ax,ay) to (bx,by), radians.
inline double getAngle(double ax, double ay, double bx, double by) {
    return std::atan2(by - ay, bx - ax);
}

struct Vec2 {
    double x = 0;
    double y = 0;
};

struct Pod {
    double x = 0;      // position
    double y = 0;
    double vx = 0;     // velocity
    double vy = 0;
    double angle = 0;     // facing, radians (0 == +x)
    int shieldtimer = 0;  // > 0 while shield active (engine off, 10x mass)
    int next = 0;           // index of the next checkpoint to reach (wraps)
    int cpPassed = 0;       // total checkpoints crossed (laps = cpPassed / numCp)
    bool boostUsed = false; // the one-shot BOOST has been consumed
};

struct Command {
    double targetX = 0;
    double targetY = 0;
    int thrust = 0;
    bool shield = false;  // SHIELD: 10x mass this turn, engine off for 3 turns
    bool boost = false;   // BOOST: 650 once per race, then 200
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

// Time within this turn at which pods a and b first touch (center distance ==
// sqrt(rsq)). Ported from robostac's newCollide: 0 if already overlapping, 10
// (i.e. "no collision this turn") if moving apart or no real root.
inline double timeToCollision(const Pod& a, const Pod& b, double rsq) {
    double px = b.x - a.x;
    double py = b.y - a.y;
    double pLen2 = px * px + py * py;
    if (pLen2 <= rsq) return 0;
    double vx = b.vx - a.vx;
    double vy = b.vy - a.vy;
    double dot = px * vx + py * vy;
    if (dot > 0) return 10;  // moving apart
    double vLen2 = vx * vx + vy * vy;
    double disc = dot * dot - vLen2 * (pLen2 - rsq);
    if (disc < 0) return 10;
    return (-dot - std::sqrt(disc)) / vLen2;
}

// Elastic collision response with mass (a shielding pod gets 10x mass), a
// minimum impulse of 120, and a tiny positional separation. Ported byte-for-byte
// from robostac's bounce.
inline void bounce(Pod& a, Pod& b) {
    double nx = b.x - a.x;
    double ny = b.y - a.y;
    double dd = std::sqrt(nx * nx + ny * ny);
    nx /= dd;
    ny /= dd;
    double rvx = a.vx - b.vx;
    double rvy = a.vy - b.vy;
    double m1 = (a.shieldtimer == 4) ? 0.1 : 1.0;
    double m2 = (b.shieldtimer == 4) ? 0.1 : 1.0;
    double force = (nx * rvx + ny * rvy) / (m1 + m2);
    if (force < MIN_IMPULSE)
        force += MIN_IMPULSE;
    else
        force += force;
    double ix = nx * -force;
    double iy = ny * -force;
    a.vx += ix * m1;
    a.vy += iy * m1;
    b.vx += -ix * m2;
    b.vy += -iy * m2;
    if (dd <= 800) {
        dd -= 800;
        a.x += nx * -(-dd / 2 + BOUNCE_EPS);
        a.y += ny * -(-dd / 2 + BOUNCE_EPS);
        b.x += nx * (-dd / 2 + BOUNCE_EPS);
        b.y += ny * (-dd / 2 + BOUNCE_EPS);
    }
}

// Advance all pods' positions by t * velocity (no friction).
inline void forwardTime(Pod* pods, int n, double t) {
    for (int i = 0; i < n; ++i) {
        pods[i].x += pods[i].vx * t;
        pods[i].y += pods[i].vy * t;
    }
}

// True if the segment from (p1x,p1y) to (p2x,p2y) passes within sqrt(cpRsq) of
// cp (i.e. the pod's center crosses the checkpoint during the move). Ported from
// robostac's cpCollide.
inline bool cpCollide(double p1x, double p1y, double p2x, double p2y,
                      const Vec2& cp, double cpRsq) {
    double dx = p2x - p1x;
    double dy = p2y - p1y;
    double ppx = p1x;
    double ppy = p1y;
    double pd2 = dx * dx + dy * dy;
    if (pd2 != 0) {
        double u = ((cp.x - p1x) * dx + (cp.y - p1y) * dy) / pd2;
        if (u > 1) {
            ppx = p2x;
            ppy = p2y;
        } else if (u > 0) {
            ppx = p1x + u * dx;
            ppy = p1y + u * dy;
        }
    }
    ppx -= cp.x;
    ppy -= cp.y;
    return (ppx * ppx + ppy * ppy) < cpRsq;
}

constexpr int MAX_PODS = 8;  // CSB/MPR uses at most 4

// Advance the whole game one turn: rotate + thrust each pod from its command,
// then move with elastic collisions resolved earliest-first (robostac's
// nextTurn), detecting checkpoint passage along the swept path, then friction +
// round + shield decay. Pass cps/numCp to enable checkpoint detection; omit them
// for pure-physics tests. Shield/boost activation and timeout land later. Set
// collisions=false to disable pod-pod collisions (the low leagues Wood 2/Wood 1).
inline void step(Pod* pods, int n, const Command* cmds, const Vec2* cps = nullptr,
                 int numCp = 0, bool firstTurn = false, bool collisions = true) {
    for (int i = 0; i < n; ++i) {
        // Resolve effective thrust: SHIELD arms the timer and cuts the engine;
        // BOOST is 650 the first time, 200 after; an active shield forces 0.
        int thr = cmds[i].thrust;
        if (cmds[i].shield) {
            pods[i].shieldtimer = 4;
            thr = 0;
        } else if (cmds[i].boost) {
            if (!pods[i].boostUsed) {
                pods[i].boostUsed = true;
                thr = 650;
            } else {
                thr = 100;  // boost already spent -> normal max thrust (real CG)
            }
        }
        if (pods[i].shieldtimer > 0) thr = 0;

        if (firstTurn) {
            pods[i].angle = getAngle(pods[i].x, pods[i].y, cmds[i].targetX, cmds[i].targetY);
        } else {
            rotateToward(pods[i], cmds[i].targetX, cmds[i].targetY);
        }
        pods[i].vx += std::cos(pods[i].angle) * thr;
        pods[i].vy += std::sin(pods[i].angle) * thr;
    }

    auto passCp = [&](int i) {
        pods[i].cpPassed++;
        pods[i].next = (pods[i].next + 1) % numCp;
    };

    double curx[MAX_PODS];
    double cury[MAX_PODS];
    for (int i = 0; i < n; ++i) {
        curx[i] = pods[i].x;
        cury[i] = pods[i].y;
    }

    if (collisions) {
        double t = 1.0;
        while (t > 0.0) {
            double first = t;
            int ci = 0;
            int cj = 0;
            for (int i = n - 1; i > 0; --i) {
                for (int j = i - 1; j >= 0; --j) {
                    double tx = timeToCollision(pods[i], pods[j], POD_RSQ);
                    if (tx <= first) {
                        first = tx;
                        ci = i;
                        cj = j;
                    }
                }
            }
            forwardTime(pods, n, first);
            t -= first;
            if (ci != cj) bounce(pods[ci], pods[cj]);
            if (t > 0.0 && cps != nullptr) {
                for (int i = 0; i < n; ++i) {
                    if (cpCollide(curx[i], cury[i], pods[i].x, pods[i].y,
                                  cps[pods[i].next], CP_RSQ)) {
                        passCp(i);
                    }
                    curx[i] = pods[i].x;
                    cury[i] = pods[i].y;
                }
            }
        }
    } else {
        // Low leagues (Wood 2, Wood 1): no pod-pod collisions — one clean sweep.
        forwardTime(pods, n, 1.0);
    }

    for (int i = 0; i < n; ++i) {
        pods[i].vx = std::trunc(pods[i].vx * FRICTION);
        pods[i].vy = std::trunc(pods[i].vy * FRICTION);
        pods[i].x = roundHalfUp(pods[i].x);
        pods[i].y = roundHalfUp(pods[i].y);
        if (pods[i].shieldtimer > 0) pods[i].shieldtimer--;
        if (cps != nullptr && cpCollide(curx[i], cury[i], pods[i].x, pods[i].y,
                                        cps[pods[i].next], CP_RSQ)) {
            passCp(i);
        }
    }
}

// Convenience wrapper: advance a single pod (no collisions possible).
inline void simulateTurn(Pod& pod, const Command& cmd, bool firstTurn = false) {
    step(&pod, 1, &cmd, nullptr, 0, firstTurn);
}

}  // namespace engine
