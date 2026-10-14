#!/usr/bin/env python3
"""Where a test's wall time goes: the RPC call log of `run.py --profile`.

    python3 pc/tests/run.py --profile -j 2 glow_stability
    python3 pc/tests/profile_report.py pc/build/tests [--top 12]

For each suite directory with an rpc-profile.txt: the time from the first
launch to the last call, the time inside game launches and RPC calls (a
step's wait included: Game.run is attributed to the test line that called
it), and the test's lines that took longest. Time outside calls is the
test's own work (image comparisons, sleeps, file writes).
"""
import argparse
import collections
import pathlib


def report(path, top):
    rows = []
    for line in path.read_text().splitlines():
        parts = line.split()
        if len(parts) >= 4:
            rows.append((float(parts[0]), float(parts[1]), parts[2], parts[3]))
    if not rows:
        return
    span = max(s + d for s, d, _, _ in rows) - min(s for s, _, _, _ in rows)
    inside = sum(d for _, d, _, _ in rows)
    sites = collections.defaultdict(lambda: [0.0, 0, collections.Counter()])
    for _, d, method, site in rows:
        entry = sites[site]
        entry[0] += d
        entry[1] += 1
        entry[2][method] += 1
    launches = [d for _, d, m, _ in rows if m == "launch"]
    print(f"== {path.parent.name}: {span:.1f}s span, {inside:.1f}s in calls, {len(launches)} launches "
          f"({sum(launches):.1f}s)")
    for site, (d, n, methods) in sorted(sites.items(), key=lambda kv: -kv[1][0])[:top]:
        shown = ", ".join(f"{m}x{c}" for m, c in methods.most_common(3))
        print(f"  {d:7.2f}s  {site:<40} {n:5d} calls ({shown})")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("dirs", nargs="+", type=pathlib.Path)
    parser.add_argument("--top", type=int, default=10)
    args = parser.parse_args()
    for d in args.dirs:
        for path in sorted(d.rglob("rpc-profile.txt")) if d.is_dir() else [d]:
            report(path, args.top)


if __name__ == "__main__":
    main()
