// Trivial Mad Pod Racing bot: head straight to the next checkpoint at full
// thrust. For smoke-testing the referee, not for competition.
#include <cstdio>

int main() {
    int x, y, cx, cy, d, a, ox, oy;
    while (std::scanf("%d %d %d %d %d %d", &x, &y, &cx, &cy, &d, &a) == 6) {
        std::scanf("%d %d", &ox, &oy);
        std::printf("%d %d 100\n", cx, cy);
        std::fflush(stdout);
    }
    return 0;
}
