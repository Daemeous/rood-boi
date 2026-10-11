#!/usr/bin/env python3
"""Read and classify an animation trace (RPC anim_trace, render.c).

The trace holds, per presented image, every object instance the image shows
(characters, weapons, props: the native object lists) with its game frame,
the blend t it was drawn at, whether it had a previous palette and whether
the jump test refused to blend, and three bone palettes: as drawn, the game
frame's own and the previous game frame's.

Instances are keyed by mesh id + occurrence (the n-th instance of that mesh
in the image), as the renderer's own previous-palette matching would see
them. For each key this reports:

  motion       per game frame, the pose change (bones relative to the root
               bone, so walking does not count) and the root's motion; per
               presented image, the change of what was drawn
  held         the pose did not change for N game frames and then jumped:
               the animation is keyed below the game frame rate
  cadence      game frame intervals in vsyncs (1/2/4) and how often they
               change while the instance moves
  stalls       presented images that repeat the one before while the
               instance moves (no in-between drawn)
  jump cuts    blends refused by the jump test, and whether the motion there
               was continuous (false positive), and unflagged steps much
               larger than their neighbours (false negative)
  shrink       blended rotations whose scale falls below the two poses'
               (linear matrix blending shortens fast rotations)
  unmatched    images where an instance had no previous palette although an
               instance of the same mesh (or object) was drawn the frame before

    python3 pc/tools/anim_trace.py trace.bin [--json out.json] [--keys]

Units: an "approximate vertex displacement" in world units: the largest
translation change of a bone plus its rotation change (max abs element)
times 128, a typical bone-to-vertex distance.
"""
import argparse
import collections
import json
import struct
import sys

import numpy as np

ARM = 128.0  # rotation change -> displacement at this distance
EPS = 0.05  # displacement below this: no change


def read(path):
    data = open(path, "rb").read()
    magic, version = struct.unpack_from("<II", data, 0)
    if magic != 0x52544E41:
        raise SystemExit(f"{path}: not an animation trace")
    pos = 8
    head = 32 if version >= 2 else 24
    frames = []
    frame_head = 24 if version >= 3 else 20
    while pos + frame_head <= len(data):
        vsync, serial, replayed, t_bits, count = struct.unpack_from("<IIIII", data, pos)
        t = struct.unpack("<f", struct.pack("<I", t_bits))[0]
        phase = struct.unpack_from("<f", data, pos + 20)[0] if version >= 3 else 0.0
        pos += frame_head
        instances = []
        for _ in range(count):
            if pos + head > len(data):
                break
            mesh, owner, list_id, list_serial, index, flags = struct.unpack_from("<IIIIII", data, pos)
            carrier = struct.unpack_from("<II", data, pos + 24) if version >= 2 else (0, 0)
            pos += head
            bones = flags & 0xFF
            has_prev = bool(flags >> 8 & 1)
            n = bones * 12
            arrays = []
            for _k in range(3 if has_prev else 2):
                arrays.append(np.frombuffer(data, "<f4", n, pos).reshape(bones, 12).astype(np.float64))
                pos += n * 4
            instances.append(dict(mesh=mesh, owner=owner, list=list_id, serial=list_serial, index=index,
                                  bones=bones, has_prev=has_prev, jumped=bool(flags >> 9 & 1),
                                  blended=bool(flags >> 10 & 1), blend=flags >> 12 & 7,
                                  carrier=(carrier[0], carrier[1] >> 8, carrier[1] & 0xFF) if carrier[0] else None,
                                  drawn=arrays[0], cur=arrays[1], prev=arrays[2] if has_prev else None))
        frame = dict(vsync=vsync, phase=phase, serial=serial, replayed=replayed, t=t, instances=instances)
        if frames and frames[-1]["vsync"] == vsync and frames[-1]["phase"] == phase:
            frames[-1] = frame  # a redraw within one vsync (pauses): the last one shows
        else:
            frames.append(frame)
    return frames


def split(p):
    return p[:, :9].reshape(-1, 3, 3), p[:, 9:]


def local_pose(p):
    """Bones relative to the root bone (0): removes the model's placement."""
    r, t = split(p)
    try:
        inv = np.linalg.inv(r[0])
    except np.linalg.LinAlgError:
        return p
    lr = np.einsum("ij,bjk->bik", inv, r)
    lt = np.einsum("ij,bj->bi", inv, t - t[0])
    return np.concatenate([lr.reshape(-1, 9), lt], axis=1)


def displacement(a, b):
    """Approximate vertex displacement between two palettes (max over bones)."""
    if a is None or b is None or a.shape != b.shape:
        return float("nan")
    d = np.abs(a - b)
    return float(np.max(np.max(d[:, :9], axis=1) * ARM + np.max(d[:, 9:], axis=1)))


def scales(p):
    r, _ = split(p)
    return np.linalg.norm(r, axis=1)  # column norms per bone (bones, 3)


def keyed(frames):
    """Per frame: {(mesh, occurrence): instance}."""
    out = []
    for f in frames:
        seen = collections.Counter()
        keys = {}
        for inst in f["instances"]:
            k = (inst["mesh"], seen[inst["mesh"]])
            seen[inst["mesh"]] += 1
            keys[k] = inst
        out.append(keys)
    return out


def runs(values):
    """Lengths of runs of equal booleans: [(value, length), ...]."""
    out = []
    for v in values:
        if out and out[-1][0] == v:
            out[-1][1] += 1
        else:
            out.append([v, 1])
    return out


def analyse(frames):
    per_frame = keyed(frames)
    keys = sorted({k for f in per_frame for k in f})
    # Game frames: the serial each presented image's instance belongs to, and
    # the vsync the newest serial arrived.
    arrival = {}
    for f in frames:
        arrival.setdefault(f["serial"], f["vsync"])
    report = dict(images=len(frames), vsyncs=[frames[0]["vsync"], frames[-1]["vsync"]] if frames else [0, 0],
                  game_frames=len(arrival), instances={})
    # images per second (60 per second, more with extra presents per vsync)
    span = (frames[-1]["vsync"] - frames[0]["vsync"] + 1) if frames else 1
    rate = 60.0 * max(1.0, round(len(frames) / span))
    report["images_per_second"] = rate
    serial_list = sorted(arrival)
    intervals = [arrival[b] - arrival[a] for a, b in zip(serial_list, serial_list[1:]) if b == a + 1]
    report["interval_histogram"] = dict(collections.Counter(intervals))
    report["interval_changes"] = int(sum(1 for a, b in zip(intervals, intervals[1:]) if a != b))
    by_serial_owner = collections.defaultdict(set)
    for f in frames:
        for inst in f["instances"]:
            by_serial_owner[inst["serial"]].add((inst["mesh"], inst["owner"]))

    for key in keys:
        seq = [(i, f.get(key)) for i, f in enumerate(per_frame)]
        present = [(i, x) for i, x in seq if x is not None]
        if len(present) < 4:
            continue
        r = dict(mesh=key[0], occurrence=key[1], images=len(present), bones=present[0][1]["bones"],
                 owners=sorted({hex(x["owner"]) for _, x in present}))
        # Game frames of this key: serial -> cur palette.
        game = {}
        for _, x in present:
            game.setdefault(x["serial"], x)
        serials = sorted(game)
        pose_steps, root_steps, step_serials = [], [], []
        for a, b in zip(serials, serials[1:]):
            if b != a + 1 or game[a]["bones"] != game[b]["bones"]:
                continue
            pose_steps.append(displacement(local_pose(game[a]["cur"]), local_pose(game[b]["cur"])))
            root_steps.append(displacement(game[a]["cur"][:1], game[b]["cur"][:1]))
            step_serials.append(b)
        pose_steps = np.array(pose_steps)
        root_steps = np.array(root_steps)
        moving = pose_steps > EPS
        r["game_steps"] = len(pose_steps)
        r["pose_step_median"] = float(np.median(pose_steps[moving])) if moving.any() else 0.0
        r["pose_step_max"] = float(pose_steps.max()) if len(pose_steps) else 0.0
        r["root_step_median"] = float(np.median(root_steps)) if len(root_steps) else 0.0
        r["moving_fraction"] = float(moving.mean()) if len(pose_steps) else 0.0
        # Held poses: zero pose change between changes (inside motion, i.e.
        # between the first and last moving step).
        # Only short holds (1-3 game frames) between changes count: longer
        # ones are an animation pausing, not one keyed below the frame rate.
        held_runs = []
        span = 0
        if moving.any():
            first, last = np.argmax(moving), len(moving) - 1 - np.argmax(moving[::-1])
            span = last - first + 1
            for v, n in runs(list(moving[first:last + 1])):
                if not v and n <= 3:
                    held_runs.append(n)
        r["held_runs"] = dict(collections.Counter(held_runs))
        r["held_fraction"] = float(sum(held_runs) / span) if span else 0.0
        # Cadence while this key moves: intervals of its own game frames.
        own_intervals = [arrival.get(s, 0) - arrival.get(s - 1, 0) for s in step_serials
                         if s in arrival and s - 1 in arrival]
        r["interval_histogram"] = dict(collections.Counter(own_intervals))
        r["interval_changes"] = int(sum(1 for a, b in zip(own_intervals, own_intervals[1:]) if a != b))
        # Presented images: change of what was drawn, image to image.
        shown, stalls, hitches, consecutive = [], 0, 0, 0
        for (i, a), (j, b) in zip(present, present[1:]):
            if j != i + 1:
                continue
            consecutive += 1
            d_pose = displacement(local_pose(a["drawn"]), local_pose(b["drawn"]))
            d_all = displacement(a["drawn"], b["drawn"])
            # new information: another game frame or another blend (else the
            # game itself repeated a frame: a hitch, not a renderer stall)
            fresh = (b["serial"] != a["serial"] or frames[j]["t"] != frames[i]["t"]) and b["has_prev"]
            shown.append((d_pose, d_all, fresh))
        shown = np.array(shown, dtype=float) if shown else np.zeros((0, 3))
        if len(shown):
            pose_motion = shown[:, 0]
            # "No change" between images: relative to this instance's own
            # motion (slow idle motions move a few hundredths per image).
            still = max(1e-4, min(EPS, 0.01 * r["pose_step_median"]))
            # A stall: the drawn pose repeats while the pose is changing
            # around it (the neighbouring images move).
            for k in range(1, len(pose_motion) - 1):
                if pose_motion[k] <= still and pose_motion[k - 1] > 4 * still and pose_motion[k + 1] > 4 * still:
                    if shown[k, 2]:
                        stalls += 1
                    else:
                        hitches += 1
            mv = pose_motion[pose_motion > still]
            # Distinct drawn poses per second while the instance animates
            # (one per image at most).
            active = np.nonzero(pose_motion > still)[0]
            if moving.any() and len(active):
                window = pose_motion[active[0]:active[-1] + 1]
                r["effective_fps"] = float(rate * np.mean(window > still)) if len(window) else 0.0
            r["drawn_step_median"] = float(np.median(mv)) if len(mv) else 0.0
            r["drawn_step_p95"] = float(np.percentile(mv, 95)) if len(mv) else 0.0
            # Unevenness: the largest image step against the median one.
            r["drawn_unevenness"] = float(np.percentile(mv, 95) / np.median(mv)) if len(mv) > 4 else 0.0
        r["drawn_stalls"] = stalls
        r["game_hitches"] = hitches
        r["image_pairs"] = consecutive
        # Blending state.
        r["blended_images"] = sum(1 for _, x in present if x["blended"])
        r["replayed_images"] = sum(1 for i, x in present if frames[i]["t"] < 1.0)
        r["no_previous_images"] = sum(1 for i, x in present if frames[i]["t"] < 1.0 and not x["has_prev"])
        # Jump cuts.
        flagged, false_pos = 0, 0
        typical = r["root_step_median"] + r["pose_step_median"]
        for s in serials:
            x = game[s]
            if x["jumped"]:
                flagged += 1
                d = displacement(x["prev"], x["cur"])
                if d < max(8 * typical, 64.0) and typical > 0:
                    false_pos += 1
        r["jump_flagged"] = flagged
        r["jump_false_positive"] = false_pos
        # Unflagged teleports: the root jumps far beyond its usual step (fast
        # limbs are not teleports: only the root's translation counts).
        false_neg = 0
        root = [float(np.max(np.abs(game[s]["cur"][0, 9:] - game[s]["prev"][0, 9:]))) if game[s]["has_prev"]
                else float("nan") for s in serials]
        finite = np.array([d for d in root if np.isfinite(d)])
        med = float(np.median(finite)) if len(finite) else 0.0
        blended_serials = {x["serial"] for _, x in present if x["blended"]}
        for s, d in zip(serials, root):
            if np.isfinite(d) and not game[s]["jumped"] and d > max(16 * med, 128.0) and s in blended_serials:
                false_neg += 1  # blended across (a camera cut shows the frame as is)
        r["jump_false_negative"] = false_neg
        # Matrix-lerp shrink: drawn scale against the blend of both poses'.
        worst = 0.0
        for i, x in present:
            if not x["blended"]:
                continue
            t = frames[i]["t"]
            expect = scales(x["prev"]) * (1 - t) + scales(x["cur"]) * t
            got = scales(x["drawn"])
            ok = expect > 1e-6
            if ok.any():
                worst = max(worst, float(np.max(1 - got[ok] / expect[ok])))
        r["max_shrink"] = worst
        # Unmatched: no previous palette, but this mesh (key) or this object
        # drew in the game frame before.
        # A game frame without a previous palette is an instance appearing
        # (nothing of it drew the frame before) or unmatched (the same mesh
        # and object drew the frame before, but the renderer's matching
        # missed it).
        unmatched, appeared = 0, 0
        for s in serials:
            x = game[s]
            if not x["has_prev"]:
                if (x["mesh"], x["owner"]) in by_serial_owner.get(s - 1, ()):
                    unmatched += 1
                elif s - 1 in by_serial_owner:
                    appeared += 1
        r["unmatched_game_frames"] = unmatched
        r["appeared_game_frames"] = appeared
        # Attachments (a weapon in a hand): how far the drawn in-betweens move
        # the root from its place in the bone of another instance that
        # carries it in both game frames (units).
        r["attached_to"], r["attachment_drift"] = attachment(present, frames, key)
        r["classes"] = classify(r)
        report["instances"][f"{key[0]:08x}#{key[1]}"] = r
    return report


def relative(parent, child):
    """child's transform (12 floats) in parent's frame."""
    pr, pt = parent[:9].reshape(3, 3), parent[9:]
    cr, ct = child[:9].reshape(3, 3), child[9:]
    try:
        inv = np.linalg.inv(pr)
    except np.linalg.LinAlgError:
        return None
    return np.concatenate([(inv @ cr).reshape(9), inv @ (ct - pt)])


def attachment(present, frames, key):
    """Find the bone (of another instance in the same images) in whose frame
    this key's root keeps its offset while that bone turns, then measure how far
    the drawn in-betweens leave it (approximate displacement)."""
    votes = collections.Counter()
    for i, x in present[:: max(1, len(present) // 20)]:
        if not x["has_prev"]:
            continue
        for k, other in enumerate(frames[i]["instances"]):
            if other is x or not other["has_prev"] or (x["owner"] and other["owner"] == x["owner"]):
                continue
            for b in range(other["bones"]):
                a = relative(other["cur"][b], x["cur"][0])
                c = relative(other["prev"][b], x["prev"][0])
                if a is None or c is None:
                    continue
                size = float(np.linalg.norm(a[9:]))
                tolerance = 3.0 + 0.02 * size
                # a root left where it was would be well off its offset in the
                # carrier's new frame (the carrier moved enough to tell)
                still = relative(other["cur"][b], np.concatenate([x["cur"][0][:9], x["prev"][0][9:]]))
                if size <= 256 and np.linalg.norm(a[9:] - c[9:]) < tolerance \
                        and still is not None and np.linalg.norm(still[9:] - c[9:]) > 8 * tolerance:
                    votes[(other["mesh"], b)] += 1
    if not votes:
        return None, 0.0
    (mesh, bone), n = votes.most_common(1)[0]
    if n < 3:
        return None, 0.0
    drift = 0.0
    for i, x in present:
        if not x["blended"]:
            continue
        for other in frames[i]["instances"]:
            if other["mesh"] != mesh or other["bones"] <= bone or not other["has_prev"]:
                continue
            want = relative(other["cur"][bone], x["cur"][0])
            got = relative(other["drawn"][bone], x["drawn"][0])
            if want is None or got is None:
                continue
            drift = max(drift, float(np.max(np.abs(want[9:] - got[9:]))))  # the offset (units)
            break
    return f"{mesh:08x}:{bone}", drift


def classify(r):
    out = []
    if r["moving_fraction"] < 0.05 and r["root_step_median"] < EPS:
        out.append("static")
    if r["held_fraction"] > 0.2 and r["held_runs"]:
        out.append("held")
    if r["interval_changes"] > 0:
        out.append("cadence")
    if r["drawn_stalls"] > 0:
        out.append("stalls")
    if r["jump_false_positive"] or r["jump_false_negative"]:
        out.append("jumpcut")
    if r["max_shrink"] > 0.02:
        out.append("shrink")
    if r["unmatched_game_frames"]:
        out.append("unmatched")
    if r["attachment_drift"] > 1.0:
        out.append("detached")
    if not out or out == ["cadence"]:
        out.insert(0, "smooth")
    return out


def summary(report, keys=False):
    lines = [f"{report['images']} images, vsyncs {report['vsyncs'][0]}-{report['vsyncs'][1]}, "
             f"{report['game_frames']} game frames, intervals {report['interval_histogram']} "
             f"({report['interval_changes']} changes)"]
    classes = collections.Counter(c for r in report["instances"].values() for c in r["classes"])
    lines.append("classes: " + ", ".join(f"{c} {n}" for c, n in sorted(classes.items())))
    if keys:
        lines.append(f"{'instance':>14} {'bones':>5} {'imgs':>5} {'move%':>5} {'held%':>5} {'runs':>12} "
                     f"{'pose':>6} {'root':>6} {'drawn':>6} {'uneven':>6} {'stall':>5} {'jump':>4} "
                     f"{'shrink':>6} {'unm':>4} {'fps':>4} {'drift':>6}  classes")
        for name, r in sorted(report["instances"].items()):
            lines.append(
                f"{name:>14} {r['bones']:>5} {r['images']:>5} {100 * r['moving_fraction']:>5.0f} "
                f"{100 * r['held_fraction']:>5.0f} {str(r['held_runs'])[:12]:>12} {r['pose_step_median']:>6.2f} "
                f"{r['root_step_median']:>6.2f} {r.get('drawn_step_median', 0):>6.2f} "
                f"{r.get('drawn_unevenness', 0):>6.2f} {r['drawn_stalls']:>5} {r['jump_flagged']:>4} "
                f"{r['max_shrink']:>6.3f} {r['unmatched_game_frames']:>4} {r.get('effective_fps', 0):>4.0f} "
                f"{r['attachment_drift']:>6.2f}  "
                + ",".join(r["classes"]))
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("trace", nargs="+")
    parser.add_argument("--json", help="write the full report here")
    parser.add_argument("--keys", action="store_true", help="one line per instance")
    args = parser.parse_args()
    reports = {}
    for path in args.trace:
        report = analyse(read(path))
        reports[path] = report
        print(f"== {path}")
        print(summary(report, args.keys))
    if args.json:
        with open(args.json, "w") as f:
            json.dump(reports, f, indent=1, default=float)
    return 0


if __name__ == "__main__":
    sys.exit(main())
