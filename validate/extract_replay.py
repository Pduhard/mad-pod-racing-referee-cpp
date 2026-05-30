#!/usr/bin/env python3
"""Extract a Mad Pod Racing CG replay (gameResult JSON) into a flat fixture for
the C++ state-parity validator.

The replay's `view` field carries the full per-turn pod state (x y vx vy ...
angle); `stdout` carries each pod's command; `refereeInput` carries the seed and
checkpoints. We emit, per turn, the pre-state, both commands, and the golden
post-state, so the C++ side can replay each turn independently through engine.h
and diff against reality.

Usage: extract_replay.py <replay.json> > fixture.txt
"""
import json
import sys

BOOST = -1
SHIELD = -2


def parse_referee_input(s):
    cps = []
    for line in s.splitlines():
        if line.startswith("map="):
            n = [int(x) for x in line[4:].split()]
            cps = [(n[i], n[i + 1]) for i in range(0, len(n), 2)]
    return cps


def pod_lines(view):
    # A pod-state line is one immediately followed by a line that is just `""`.
    lines = view.split("\n")
    return [lines[i].split() for i in range(len(lines) - 1) if lines[i + 1].strip() == '""']


def state(toks):
    # toks: x y vx vy thrust ? nextCpX nextCpY angle ...
    x, y, vx, vy = float(toks[0]), float(toks[1]), float(toks[2]), float(toks[3])
    ang = toks[8] if len(toks) > 8 else "null"
    ang = 0.0 if ang == "null" else float(ang)
    return (x, y, vx, vy, ang)


def cmd(out):
    a, b, t = out.split()[:3]
    thr = BOOST if t == "BOOST" else SHIELD if t == "SHIELD" else int(t)
    return (int(a), int(b), thr)


def main():
    data = json.load(open(sys.argv[1]))
    cps = parse_referee_input(data["refereeInput"])
    cmds = {}
    states = []
    for f in data["frames"]:
        out = f.get("stdout", "").strip()
        if out:
            cmds.setdefault(f["agentId"], []).append(cmd(out))
        pods = pod_lines(f.get("view", ""))
        if len(pods) >= 2:
            states.append([state(pods[0]), state(pods[1])])

    a = sorted(cmds)
    n_turns = min(len(cmds[a[0]]), len(cmds[a[1]]), len(states) - 1)

    diag = (f"# checkpoints={cps} agents={a} "
            f"cmds_per_agent={[len(cmds[x]) for x in a]} states={len(states)} "
            f"-> turns={n_turns}")
    sys.stderr.write(diag + "\n")

    out = [str(len(cps))]
    out += [f"{x} {y}" for x, y in cps]
    out.append(str(n_turns))
    for k in range(n_turns):
        pre0, pre1 = states[k]
        post0, post1 = states[k + 1]
        c0, c1 = cmds[a[0]][k], cmds[a[1]][k]
        first = 1 if k == 0 else 0
        row = [first, *c0, *c1, *pre0, *pre1, *post0, *post1]
        out.append(" ".join(repr(v) if isinstance(v, float) else str(v) for v in row))
    sys.stdout.write("\n".join(out) + "\n")


if __name__ == "__main__":
    main()
