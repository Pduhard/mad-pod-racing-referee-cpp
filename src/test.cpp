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

int main() {
    thrust_straight_from_rest();

    std::fprintf(stderr, "\n%d passed, %d failed\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
