// State-parity validator: replay a real CG Mad Pod Racing game through engine.h
// and diff every turn against the recorded pod states.
//
// Fixture format (from validate/extract_replay.py):
//   <ncp>
//   <cpx> <cpy>            x ncp
//   <nturns>
//   per turn: first c0tx c0ty c0thr c1tx c1ty c1thr
//             pre0(x y vx vy ang) pre1(...) post0(...) post1(...)
//   thrust encoded: -1 = BOOST, -2 = SHIELD, else literal thrust.
#include "../src/engine.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace engine;

static Command mkcmd(int tx, int ty, int thr) {
    Command c;
    c.targetX = tx;
    c.targetY = ty;
    if (thr == -1)
        c.boost = true;
    else if (thr == -2)
        c.shield = true;
    else
        c.thrust = thr;
    return c;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s fixture.txt\n", argv[0]);
        return 2;
    }
    std::FILE* f = std::fopen(argv[1], "r");
    if (!f) {
        std::perror("open");
        return 2;
    }

    int ncp = 0;
    (void)std::fscanf(f, "%d", &ncp);
    std::vector<Vec2> cps(ncp);
    for (int i = 0; i < ncp; i++) (void)std::fscanf(f, "%lf %lf", &cps[i].x, &cps[i].y);
    int nturns = 0;
    (void)std::fscanf(f, "%d", &nturns);

    Pod pods[2];
    bool inited = false;
    const double posTol = 0.5;   // positions/velocities are integer-valued
    const double angTol = 1e-6;
    int passes = 0;

    for (int k = 0; k < nturns; k++) {
        int first, c0[3], c1[3];
        double pre0[5], pre1[5], post0[5], post1[5];
        (void)std::fscanf(f, "%d %d %d %d %d %d %d", &first, &c0[0], &c0[1], &c0[2], &c1[0],
                          &c1[1], &c1[2]);
        for (double* a : {pre0, pre1, post0, post1})
            for (int i = 0; i < 5; i++) (void)std::fscanf(f, "%lf", &a[i]);

        if (!inited) {
            pods[0] = Pod{pre0[0], pre0[1], pre0[2], pre0[3], pre0[4]};
            pods[1] = Pod{pre1[0], pre1[1], pre1[2], pre1[3], pre1[4]};
            inited = true;
        }

        Command cmds[2] = {mkcmd(c0[0], c0[1], c0[2]), mkcmd(c1[0], c1[1], c1[2])};
        step(pods, 2, cmds, cps.data(), ncp, first == 1);

        const double* g[2] = {post0, post1};
        for (int p = 0; p < 2; p++) {
            if (std::fabs(pods[p].x - g[p][0]) > posTol || std::fabs(pods[p].y - g[p][1]) > posTol ||
                std::fabs(pods[p].vx - g[p][2]) > posTol ||
                std::fabs(pods[p].vy - g[p][3]) > posTol ||
                std::fabs(pods[p].angle - g[p][4]) > angTol) {
                std::printf(
                    "DIVERGE turn %d pod %d:\n  got  (%.1f, %.1f) v(%.1f, %.1f) a=%.6f\n"
                    "  want (%.1f, %.1f) v(%.1f, %.1f) a=%.6f\n",
                    k, p, pods[p].x, pods[p].y, pods[p].vx, pods[p].vy, pods[p].angle, g[p][0],
                    g[p][1], g[p][2], g[p][3], g[p][4]);
                std::printf("PARTIAL: %d/%d turns passed before divergence\n", passes, nturns);
                return 1;
            }
        }
        passes++;
    }
    std::printf("ALL PASS: %d/%d turns byte-exact\n", passes, nturns);
    return 0;
}
