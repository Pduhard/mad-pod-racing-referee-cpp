// csb-referee — a Mad Pod Racing referee for the cg-colosseum / Brutaltester
// protocol, built on the byte-exact engine.h physics core.
//
//   csb-referee -p1 "<bot1 cmd>" -p2 "<bot2 cmd>" -d "seed=N" [-d "laps=N"] [-d "league=6"]
//
// Spawns each bot and runs a race, printing one integer score per player on
// stdout (higher = better), exit 0. The league flag mirrors CodinGame's own
// leagueLevel: an iso-CG number 1..6 (wood2..legend) or a league name. Unset
// defaults to "legend" (top) = 2 pods/player + the raw protocol (§4b); the lower
// leagues use 1 pod/player + the pre-computed protocol (§4a).
//
// Map generation is deterministic per seed but NOT CG-online-identical (CG's
// map RNG isn't reversed yet) — fine for fair, repeatable local games. The
// physics is the validated engine.h.
#include "engine.h"

#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
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

// Pod facing as an integer degree in [0, 360), for the raw (Gold) protocol.
static int angleDeg360(double rad) {
    double d = std::fmod(rad * 180.0 / M_PI, 360.0);
    if (d < 0) d += 360.0;
    return round_i(d);
}

// CodinGame's six leagues, identical to the leagueLevel ladder: each unlocks a
// mechanic; Gold also adds the 2nd pod and the raw protocol. Legend == Gold.
struct League {
    int ppp;          // pods per player (1 low leagues, 2 Gold/Legend)
    bool raw;         // raw protocol (§4b) vs pre-computed (§4a)
    bool boost;       // BOOST unlocked
    bool shield;      // SHIELD unlocked
    bool collisions;  // pod-pod collisions enabled
};

static League leagueOf(const std::string& name) {
    //                     ppp  raw    boost  shield collisions
    if (name == "wood2")  return {1, false, false, false, false};
    if (name == "wood1")  return {1, false, true,  false, false};
    if (name == "bronze") return {1, false, true,  false, true};
    if (name == "gold" || name == "legend")
                          return {2, true,  true,  true,  true};
    return                       {1, false, true,  true,  true};  // silver (default)
}

int main(int argc, char** argv) {
    std::string p1, p2;
    uint64_t seed = 0;
    int laps = 3;
    // Top league by default (iso-CG: an unset league runs the full game).
    std::string leagueName = "legend";  // wood2|wood1|bronze|silver|gold|legend
    for (int i = 1; i < argc; i++) {
        if (!std::strcmp(argv[i], "-p1") && i + 1 < argc)
            p1 = argv[++i];
        else if (!std::strcmp(argv[i], "-p2") && i + 1 < argc)
            p2 = argv[++i];
        else if (!std::strcmp(argv[i], "-d") && i + 1 < argc) {
            std::string kv = argv[++i];
            if (kv.rfind("seed=", 0) == 0) seed = std::strtoull(kv.c_str() + 5, nullptr, 10);
            else if (kv.rfind("laps=", 0) == 0) laps = std::atoi(kv.c_str() + 5);
            else if (kv.rfind("league=", 0) == 0) {
                std::string v = kv.substr(7);
                // Accept an iso-CG numeric level (1..6) or a league name.
                bool numeric =
                    !v.empty() &&
                    std::all_of(v.begin(), v.end(), [](char c) { return c >= '0' && c <= '9'; });
                if (numeric) {
                    static const char* const LADDER[6] = {"wood2", "wood1", "bronze",
                                                          "silver", "gold", "legend"};
                    int n = std::atoi(v.c_str());
                    n = n < 1 ? 1 : (n > 6 ? 6 : n);
                    leagueName = LADDER[n - 1];
                } else {
                    leagueName = v;
                }
            }
        }
    }
    const League lg = leagueOf(leagueName);
    std::fprintf(stderr, "League: %s (%d pod%s/player)\n", leagueName.c_str(), lg.ppp,
                 lg.ppp == 1 ? "" : "s");

    std::vector<Vec2> cps = genMap(seed);
    int numCp = (int)cps.size();
    int target = laps * numCp;       // checkpoints to cross to finish
    const int ppp = lg.ppp;          // pods per player
    const int nPods = 2 * ppp;
    auto playerOf = [&](int pod) { return pod / ppp; };

    // Start pods near cp0, offset perpendicular to the cp0->cp1 direction.
    double dx = cps[1].x - cps[0].x, dy = cps[1].y - cps[0].y;
    double len = std::sqrt(dx * dx + dy * dy);
    double px = -dy / len, py = dx / len;  // unit perpendicular
    const double offSilver[2] = {500, -500};
    const double offGold[4] = {1500, 500, -500, -1500};
    Pod pods[4];
    for (int p = 0; p < nPods; p++) {
        double off = (ppp == 2) ? offGold[p] : offSilver[p];
        pods[p].x = round_i(cps[0].x + px * off);
        pods[p].y = round_i(cps[0].y + py * off);
        pods[p].next = 1;  // first target is cp1
        pods[p].angle = std::atan2(cps[1].y - pods[p].y, cps[1].x - pods[p].x);
    }

    Bot bots[2] = {spawnBot(p1), spawnBot(p2)};

    // Init block — raw protocol only: laps + the full checkpoint list.
    if (lg.raw) {
        for (int i = 0; i < 2; i++) {
            std::fprintf(bots[i].in, "%d\n%d\n", laps, numCp);
            for (auto& cp : cps)
                std::fprintf(bots[i].in, "%d %d\n", round_i(cp.x), round_i(cp.y));
            std::fflush(bots[i].in);
        }
    }

    int lastCpTurn[4] = {0, 0, 0, 0};
    int finishOrder[2] = {-1, -1};
    int finishedCount = 0;
    int loser = -1;  // eliminated by timeout / death

    for (int turn = 0; turn < MAX_TURNS && finishedCount == 0 && loser < 0; turn++) {
        // Send each player its view of the world.
        for (int i = 0; i < 2; i++) {
            if (lg.raw) {
                // Raw state — your two pods first, then the opponent's two.
                int order[4] = {i * 2, i * 2 + 1, (1 - i) * 2, (1 - i) * 2 + 1};
                for (int o : order) {
                    const Pod& p = pods[o];
                    std::fprintf(bots[i].in, "%d %d %d %d %d %d\n", round_i(p.x), round_i(p.y),
                                 round_i(p.vx), round_i(p.vy), angleDeg360(p.angle), p.next);
                }
            } else {
                const Vec2& cp = cps[pods[i].next];
                const Pod& opp = pods[1 - i];
                double ddx = cp.x - pods[i].x, ddy = cp.y - pods[i].y;
                int dist = round_i(std::sqrt(ddx * ddx + ddy * ddy));
                std::fprintf(bots[i].in, "%d %d %d %d %d %d\n", round_i(pods[i].x), round_i(pods[i].y),
                             round_i(cp.x), round_i(cp.y), dist, angleToCp(pods[i], cp));
                std::fprintf(bots[i].in, "%d %d\n", round_i(opp.x), round_i(opp.y));
            }
            std::fflush(bots[i].in);
        }

        // Read ppp command line(s) per player.
        Command cmds[4];
        for (int i = 0; i < 2 && loser < 0; i++) {
            for (int k = 0; k < ppp; k++) {
                char line[256] = {0};
                if (!std::fgets(line, sizeof line, bots[i].out)) { loser = i; break; }
                cmds[i * ppp + k] = parseCmd(line);
            }
        }
        if (loser >= 0) break;

        // Gate abilities not unlocked in this league.
        for (int p = 0; p < nPods; p++) {
            if (!lg.boost) cmds[p].boost = false;
            if (!lg.shield) cmds[p].shield = false;
        }
        int before[4];
        for (int p = 0; p < nPods; p++) before[p] = pods[p].cpPassed;
        step(pods, nPods, cmds, cps.data(), numCp, turn == 0, lg.collisions);

        for (int p = 0; p < nPods; p++) {
            if (pods[p].cpPassed > before[p]) lastCpTurn[p] = turn;
            int pl = playerOf(p);
            if (pods[p].cpPassed >= target && finishOrder[pl] < 0)
                finishOrder[pl] = finishedCount++;
        }
        // A player is eliminated only when ALL its pods are stale (no CP in TIMEOUT turns).
        for (int pl = 0; pl < 2 && loser < 0; pl++) {
            int recent = 0;
            for (int k = 0; k < ppp; k++) recent = std::max(recent, lastCpTurn[pl * ppp + k]);
            if (turn - recent >= TIMEOUT_TURNS) loser = pl;
        }
    }

    // Score per player: finishers first (earliest best), else the best pod's progress.
    auto podProgress = [&](int p) -> long long {
        double ddx = cps[pods[p].next].x - pods[p].x, ddy = cps[pods[p].next].y - pods[p].y;
        return (long long)pods[p].cpPassed * 1000000LL - (long long)std::sqrt(ddx * ddx + ddy * ddy);
    };
    auto score = [&](int pl) -> long long {
        if (loser == pl) return -1;
        if (finishOrder[pl] >= 0) return 1000000000LL - finishOrder[pl];
        long long best = podProgress(pl * ppp);
        for (int k = 1; k < ppp; k++) best = std::max(best, podProgress(pl * ppp + k));
        return best;
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
