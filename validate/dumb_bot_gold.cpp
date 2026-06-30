// Trivial Gold (raw §4b) bot: each of your two pods heads straight to its next
// checkpoint at full thrust. For smoke-testing the referee, not competition.
#include <cstdio>
#include <vector>

int main() {
    int laps, n;
    if (std::scanf("%d %d", &laps, &n) != 2) return 0;
    std::vector<int> cx(n), cy(n);
    for (int i = 0; i < n; i++) std::scanf("%d %d", &cx[i], &cy[i]);
    for (;;) {
        int x, y, vx, vy, a, ncp, myNext[2];
        bool ok = true;
        for (int p = 0; p < 4; p++) {  // my 2 pods, then 2 opponents
            if (std::scanf("%d %d %d %d %d %d", &x, &y, &vx, &vy, &a, &ncp) != 6) { ok = false; break; }
            if (p < 2) myNext[p] = ncp;
        }
        if (!ok) break;
        for (int p = 0; p < 2; p++) std::printf("%d %d 100\n", cx[myNext[p]], cy[myNext[p]]);
        std::fflush(stdout);
    }
    return 0;
}
