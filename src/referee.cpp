// csb-referee — a Mad Pod Racing referee for the cg-colosseum / Brutaltester
// protocol, built on the byte-exact engine.h physics core.
//
//   csb-referee -p1 "<bot1 cmd>" -p2 "<bot2 cmd>" -d "seed=N" [-d "laps=N"]
//
// Spawns each bot, runs the low-league (1 pod per player) race, prints one
// integer score per player on stdout (higher = better), exit 0.
//
// Map generation is deterministic per seed but NOT CG-online-identical (CG's
// map RNG isn't reversed yet) — fine for fair, repeatable local games. The
// physics is the validated engine.h.
#include "engine.h"

#include <sys/wait.h>
#include <unistd.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace engine;

constexpr double MAP_W = 16000, MAP_H = 9000;
constexpr int TIMEOUT_TURNS = 100;
constexpr int MAX_TURNS = 1500;

// splitmix64 — deterministic seeded RNG.
struct Rng {
    uint64_t s;
    uint64_t next() {
        s += 0x9E3779B97F4A7C15ULL;
        uint64_t z = s;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }
    double uni() { return (next() >> 11) * (1.0 / 9007199254740992.0); }
    int range(int lo, int hi) { return lo + (int)(uni() * (hi - lo + 1)); }
};

// Deterministic map: 3-5 checkpoints inside a margin, min separation enforced.
static std::vector<Vec2> genMap(uint64_t seed) {
    Rng rng{seed};
    int n = rng.range(3, 5);
    std::vector<Vec2> cps;
    const double margin = 1000, minSep = 3000;
    int guard = 0;
    while ((int)cps.size() < n && guard++ < 10000) {
        Vec2 c{margin + rng.uni() * (MAP_W - 2 * margin), margin + rng.uni() * (MAP_H - 2 * margin)};
        bool ok = true;
        for (auto& p : cps) {
            double dx = p.x - c.x, dy = p.y - c.y;
            if (dx * dx + dy * dy < minSep * minSep) {
                ok = false;
                break;
            }
        }
        if (ok) cps.push_back(c);
    }
    return cps;
}

struct Bot {
    pid_t pid;
    FILE* in;
    FILE* out;
};

static Bot spawnBot(const std::string& cmd) {
    int toBot[2], fromBot[2];
    if (pipe(toBot) || pipe(fromBot)) {
        std::perror("pipe");
        std::exit(2);
    }
    pid_t pid = fork();
    if (pid == 0) {
        dup2(toBot[0], 0);
        dup2(fromBot[1], 1);
        close(toBot[0]);
        close(toBot[1]);
        close(fromBot[0]);
        close(fromBot[1]);
        execl("/bin/sh", "sh", "-c", cmd.c_str(), (char*)nullptr);
        _exit(127);
    }
    close(toBot[0]);
    close(fromBot[1]);
    return Bot{pid, fdopen(toBot[1], "w"), fdopen(fromBot[0], "r")};
}

static int round_i(double x) { return (int)std::floor(x + 0.5); }

// signed angle (degrees, [-180,180]) from pod facing to the next checkpoint.
static int angleToCp(const Pod& p, const Vec2& cp) {
    double a = std::atan2(cp.y - p.y, cp.x - p.x) - p.angle;
    while (a > M_PI) a -= 2 * M_PI;
    while (a < -M_PI) a += 2 * M_PI;
    return round_i(a * 180.0 / M_PI);
}

static Command parseCmd(const char* line) {
    Command c;
    char thr[64] = {0};
    int tx = 0, ty = 0;
    if (std::sscanf(line, "%d %d %63s", &tx, &ty, thr) >= 2) {
        c.targetX = tx;
        c.targetY = ty;
        if (std::strcmp(thr, "BOOST") == 0)
            c.boost = true;
        else if (std::strcmp(thr, "SHIELD") == 0)
            c.shield = true;
        else
            c.thrust = std::atoi(thr);
    }
    return c;
}

int main(int argc, char** argv) {
    std::string p1, p2;
    uint64_t seed = 0;
    int laps = 3;
    for (int i = 1; i < argc; i++) {
        if (!std::strcmp(argv[i], "-p1") && i + 1 < argc)
            p1 = argv[++i];
        else if (!std::strcmp(argv[i], "-p2") && i + 1 < argc)
            p2 = argv[++i];
        else if (!std::strcmp(argv[i], "-d") && i + 1 < argc) {
            std::string kv = argv[++i];
            if (kv.rfind("seed=", 0) == 0) seed = std::strtoull(kv.c_str() + 5, nullptr, 10);
            else if (kv.rfind("laps=", 0) == 0) laps = std::atoi(kv.c_str() + 5);
        }
    }

    std::vector<Vec2> cps = genMap(seed);
    int numCp = (int)cps.size();
    int target = laps * numCp;  // checkpoints to cross to finish

    // Start both pods at cp0, offset perpendicular to the cp0->cp1 direction.
    double dx = cps[1].x - cps[0].x, dy = cps[1].y - cps[0].y;
    double len = std::sqrt(dx * dx + dy * dy);
    double px = -dy / len, py = dx / len;  // unit perpendicular
    Pod pods[2];
    for (int i = 0; i < 2; i++) {
        double off = (i == 0 ? 500.0 : -500.0);
        pods[i].x = round_i(cps[0].x + px * off);
        pods[i].y = round_i(cps[0].y + py * off);
        pods[i].next = 1;  // first target is cp1
        pods[i].angle = std::atan2(cps[1].y - pods[i].y, cps[1].x - pods[i].x);
    }

    Bot bots[2] = {spawnBot(p1), spawnBot(p2)};
    int lastCpTurn[2] = {0, 0};
    int finishOrder[2] = {-1, -1};
    int finishedCount = 0;
    int loser = -1;  // eliminated by timeout

    for (int turn = 0; turn < MAX_TURNS && finishedCount == 0 && loser < 0; turn++) {
        for (int i = 0; i < 2; i++) {
            const Vec2& cp = cps[pods[i].next];
            const Pod& opp = pods[1 - i];
            double ddx = cp.x - pods[i].x, ddy = cp.y - pods[i].y;
            int dist = round_i(std::sqrt(ddx * ddx + ddy * ddy));
            std::fprintf(bots[i].in, "%d %d %d %d %d %d\n", round_i(pods[i].x), round_i(pods[i].y),
                         round_i(cp.x), round_i(cp.y), dist, angleToCp(pods[i], cp));
            std::fprintf(bots[i].in, "%d %d\n", round_i(opp.x), round_i(opp.y));
            std::fflush(bots[i].in);
        }
        Command cmds[2];
        for (int i = 0; i < 2; i++) {
            char line[256] = {0};
            if (!std::fgets(line, sizeof line, bots[i].out)) {
                loser = i;  // bot died / no output
            } else {
                cmds[i] = parseCmd(line);
            }
        }
        if (loser >= 0) break;

        int before[2] = {pods[0].cpPassed, pods[1].cpPassed};
        step(pods, 2, cmds, cps.data(), numCp, turn == 0);

        for (int i = 0; i < 2; i++) {
            if (pods[i].cpPassed > before[i]) lastCpTurn[i] = turn;
            if (pods[i].cpPassed >= target && finishOrder[i] < 0) {
                finishOrder[i] = finishedCount++;
            }
            if (turn - lastCpTurn[i] >= TIMEOUT_TURNS) loser = i;
        }
    }

    // Score: finishers rank first (earliest finish best), then by progress.
    auto score = [&](int i) -> long long {
        if (loser == i) return -1;
        if (finishOrder[i] >= 0) return 1000000000LL - finishOrder[i];
        double ddx = cps[pods[i].next].x - pods[i].x, ddy = cps[pods[i].next].y - pods[i].y;
        return (long long)pods[i].cpPassed * 1000000LL - (long long)std::sqrt(ddx * ddx + ddy * ddy);
    };
    std::printf("%lld\n%lld\n", score(0), score(1));

    for (int i = 0; i < 2; i++) {
        if (bots[i].in) std::fclose(bots[i].in);
        if (bots[i].out) std::fclose(bots[i].out);
        kill(bots[i].pid, SIGKILL);
        waitpid(bots[i].pid, nullptr, 0);
    }
    return 0;
}
