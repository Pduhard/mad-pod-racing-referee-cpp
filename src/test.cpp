// Unit + parity tests for the Mad Pod Racing physics engine.
//
// Expected values are derived from robostac/coders-strike-back-referee (Go,
// MIT), itself validated byte-exact against 500+ real CodinGame replays.
#include "engine.h"

#include <cmath>
#include <cstdio>

static int passed = 0;
static int failed = 0;

static bool near(double a, double b) { return std::fabs(a - b) < 1e-6; }

#define CHECK(name, cond)                                                      \
    do {                                                                       \
        if (cond) {                                                            \
            passed++;                                                          \
        } else {                                                               \
            std::fprintf(stderr, "FAIL: %s (line %d)\n", (name), __LINE__);    \
            failed++;                                                          \
        }                                                                      \
    } while (0)

using namespace engine;

// A pod at rest facing +x, told to thrust 100 toward a point straight ahead,
// advances by its new velocity, then keeps trunc(v * 0.85) of it.
static void thrust_straight_from_rest() {
    Pod pod;  // (0,0), velocity (0,0), angle 0 == facing +x
    Command cmd;
    cmd.targetX = 10000;
    cmd.targetY = 0;
    cmd.thrust = 100;

    simulateTurn(pod, cmd);

    CHECK("pos.x == 100", near(pod.x, 100));
    CHECK("pos.y == 0", near(pod.y, 0));
    CHECK("vel.x == trunc(100*0.85) == 85", near(pod.vx, 85));
    CHECK("vel.y == 0", near(pod.vy, 0));
}

// CG/robostac round is floor(x + 0.5) — HALF_UP toward +inf, NOT std::round
// (which is half-away-from-zero: std::round(-2.5) == -3).
static void round_is_half_up() {
    CHECK("round(2.5) == 3", near(roundHalfUp(2.5), 3));
    CHECK("round(2.4) == 2", near(roundHalfUp(2.4), 2));
    CHECK("round(-2.5) == -2 (half up, not away-from-zero)",
          near(roundHalfUp(-2.5), -2));
}

// Rotating toward a target more than 18 deg away caps the turn at 18 deg.
static void rotation_caps_at_18_degrees() {
    Pod pod;  // angle 0, facing +x
    Command cmd;
    cmd.targetX = 0;
    cmd.targetY = 10000;  // straight up: +90 deg, well past the cap
    cmd.thrust = 0;       // isolate rotation
    simulateTurn(pod, cmd);
    CHECK("angle capped to +18 deg", near(pod.angle, MAX_ROTATE));
}

// Within the cap, the pod snaps directly to the target heading.
static void rotation_snaps_when_within_cap() {
    Pod pod;
    Command cmd;
    cmd.targetX = std::cos(0.1) * 1000;  // 0.1 rad above +x (< 18 deg)
    cmd.targetY = std::sin(0.1) * 1000;
    cmd.thrust = 0;
    simulateTurn(pod, cmd);
    CHECK("angle snaps to target heading (0.1 rad)", near(pod.angle, 0.1));
}

// On the first turn the pod faces its target instantly (no 18 deg cap).
static void first_turn_faces_target_instantly() {
    Pod pod;  // angle 0
    Command cmd;
    cmd.targetX = 0;
    cmd.targetY = 10000;  // straight up
    cmd.thrust = 0;
    simulateTurn(pod, cmd, /*firstTurn=*/true);
    CHECK("first turn faces target (pi/2), uncapped", near(pod.angle, M_PI / 2));
}

// The first-turn heading keeps the raw signed atan2 value; CG does NOT wrap it
// to [0, 2pi) (verified byte-exact against a real replay).
static void first_turn_keeps_signed_angle() {
    Pod pod;
    pod.x = 1000;
    pod.y = 1000;
    Command cmd;
    cmd.targetX = 0;  // down-left: atan2(-1000,-1000) = -3pi/4, not +5pi/4
    cmd.targetY = 0;
    cmd.thrust = 0;
    simulateTurn(pod, cmd, true);
    CHECK("first-turn angle stays signed (-3pi/4)",
          near(pod.angle, std::atan2(-1000.0, -1000.0)));
}

// Two pods closing head-on at equal speed swap velocities (elastic, equal
// mass), then friction trims each to trunc(v*0.85). Hand-derived from
// robostac's bounce: gap 1000, closing 400/turn -> collide at t=0.5; post-bounce
// each |v|=200 reversed; remaining 0.5 turn returns them to start; friction
// trunc(200*0.85)=170.
static void head_on_collision_swaps_velocities() {
    Pod a;
    a.x = 0;
    a.vx = 200;  // facing +x
    Pod b;
    b.x = 1000;
    b.vx = -200;
    b.angle = M_PI;  // facing -x
    Pod pods[2] = {a, b};
    Command cmds[2];
    cmds[0].targetX = 10000;
    cmds[0].thrust = 0;  // keep heading, no thrust
    cmds[1].targetX = -10000;
    cmds[1].thrust = 0;

    step(pods, 2, cmds);

    CHECK("pod A vx reversed then friction == -170", near(pods[0].vx, -170));
    CHECK("pod B vx reversed then friction == 170", near(pods[1].vx, 170));
    CHECK("pod A returns near x=0", near(pods[0].x, 0));
    CHECK("pod B returns near x=1000", near(pods[1].x, 1000));
}

// With collisions disabled (low leagues Wood 2 / Wood 1), two head-on pods pass
// through each other: each keeps its direction, only friction trims the speed.
static void head_on_no_collision_passes_through() {
    Pod a;
    a.x = 0;
    a.vx = 200;
    Pod b;
    b.x = 1000;
    b.vx = -200;
    b.angle = M_PI;
    Pod pods[2] = {a, b};
    Command cmds[2];
    cmds[0].targetX = 10000;
    cmds[0].thrust = 0;
    cmds[1].targetX = -10000;
    cmds[1].thrust = 0;

    step(pods, 2, cmds, nullptr, 0, /*firstTurn=*/false, /*collisions=*/false);

    CHECK("no collision: pod A keeps +x, friction -> 170", near(pods[0].vx, 170));
    CHECK("no collision: pod B keeps -x, friction -> -170", near(pods[1].vx, -170));
    CHECK("pod A passed through to x=200", near(pods[0].x, 200));
    CHECK("pod B passed through to x=800", near(pods[1].x, 800));
}

// A pod whose movement path passes within 600 of its next checkpoint advances
// its `next` index (and is not "won" while more checkpoints remain).
static void crossing_checkpoint_advances_next() {
    Pod pod;
    pod.x = 500;
    pod.vx = 600;  // will move to x=1100 this turn, passing through (1000,0)
    Vec2 cps[2] = {{1000, 0}, {5000, 0}};
    Command cmd;
    cmd.targetX = 5000;  // straight ahead: no rotation
    cmd.thrust = 0;

    step(&pod, 1, &cmd, cps, 2);

    CHECK("next advanced 0 -> 1 after crossing cp0", pod.next == 1);
    CHECK("one checkpoint counted", pod.cpPassed == 1);
}

// SHIELD: no thrust this turn, and the shield timer is set (then decays 4->3).
static void shield_zeroes_thrust_and_arms_timer() {
    Pod pod;
    Command cmd;
    cmd.targetX = 10000;
    cmd.thrust = 100;  // ignored when shielding
    cmd.shield = true;
    simulateTurn(pod, cmd);
    CHECK("shield => no velocity", near(pod.vx, 0) && near(pod.vy, 0));
    CHECK("shieldtimer set to 4 then decayed to 3", pod.shieldtimer == 3);
}

// First BOOST of the race gives thrust 650; friction -> trunc(650*0.85)=552.
static void boost_first_use_is_650() {
    Pod pod;  // facing +x
    Command cmd;
    cmd.targetX = 10000;
    cmd.boost = true;
    simulateTurn(pod, cmd);
    CHECK("boost moves to x=650", near(pod.x, 650));
    CHECK("vx after friction == trunc(650*0.85) == 552", near(pod.vx, 552));
    CHECK("boost marked used", pod.boostUsed == true);
}

// A BOOST after the one-shot is spent applies the normal max thrust 100, not
// 200 (verified byte-exact against a real CG replay). friction -> 85.
static void boost_after_spent_is_thrust_100() {
    Pod pod;
    pod.boostUsed = true;
    Command cmd;
    cmd.targetX = 10000;
    cmd.boost = true;
    simulateTurn(pod, cmd);
    CHECK("spent boost == thrust 100 -> friction trunc(100*0.85)==85",
          near(pod.vx, 85));
}

// While a shield is still active (timer > 0), the engine stays off.
static void active_shield_blocks_thrust() {
    Pod pod;
    pod.shieldtimer = 2;
    Command cmd;
    cmd.targetX = 10000;
    cmd.thrust = 100;
    simulateTurn(pod, cmd);
    CHECK("active shield => thrust ignored, no velocity", near(pod.vx, 0));
    CHECK("shieldtimer decayed 2 -> 1", pod.shieldtimer == 1);
}

int main() {
    thrust_straight_from_rest();
    round_is_half_up();
    rotation_caps_at_18_degrees();
    rotation_snaps_when_within_cap();
    first_turn_faces_target_instantly();
    first_turn_keeps_signed_angle();
    head_on_collision_swaps_velocities();
    head_on_no_collision_passes_through();
    crossing_checkpoint_advances_next();
    shield_zeroes_thrust_and_arms_timer();
    boost_first_use_is_650();
    boost_after_spent_is_thrust_100();
    active_shield_blocks_thrust();

    std::fprintf(stderr, "\n%d passed, %d failed\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
