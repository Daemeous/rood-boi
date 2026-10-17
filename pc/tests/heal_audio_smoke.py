#!/usr/bin/env python3
"""The sound after a Cure Root decays: no endless echo.

    python3 pc/tests/heal_audio_smoke.py [--card pc/build/test-card1.mcd] [--out DIR]

An item's effect sound is streamed (src 25AC.c Sound_Cutscene_StartStream):
two voices play a double buffer in SPU RAM at 0x1100 / 0x2100, and an SPU
address interrupt at each half's start (SpuSetIRQAddr) makes the driver load
the next 2 KiB of the stream; after the last one, the interrupt at the silent
block 0x1030 keys the voices off (IRQCallbackProc), as does the driver's
stop command. Two host gaps made it an endless echo: the interrupt was not
raised (the voices looped the stream's second 2 KiB, a 0.16 s phrase), and
the stop's key off was lost (key on/off registers kept only the last write
before the register sync, and the driver keyed another voice off in the same
tick), so the phrase repeated until a room change or a save point keyed the
voices off (pc/src/audio_libspu.c SpuSetIRQ; 25AC.c SetVoiceKeyOff under
ROOD_PC).

Needs a card whose first save is in the save room (skipped otherwise).
Continues it (save state, fast), records the audio (ROOD_AUDIO=wav), uses a
Cure Root through the modern menus (Items > Misc, the first row) with the
battle charging option off, and runs 12 s more in the field. Checks:
  - the Cure Root was used (the stack one less) and its sound was streamed
    (g_Sound_Cutscene_StreamState.VoicesInUseFlags set during the action);
  - the stream was fed: its bytes still to load (TotalPages) seen at three
    or more values (one without the interrupt);
  - the driver ended the stream: VoicesInUseFlags back to 0 before the
    field is back;
  - 2 s after the field is back (RPC spu) the stream's voices are keyed
    off and silent, the SPU interrupt is off, and the reverb (master enable,
    the stream voices' sends) is as before the heal;
  - the audio from 2 s to 10 s after the field is back is the room's again:
    its level at most 1.3x the 5 s before the menu opened, and no repeating
    phrase (normalised autocorrelation of the samples over 50 ms..1 s lags
    below 0.4; the loop measured 0.64 at 7168 samples, the room ~0.1).
With --no-irq (ROOD_SPU_IRQ=0) the interrupt is not served: the stream is
fed once and loops until the driver's stop command (func_8001D698), which
the game thread sends between two driver ticks; the same checks then test
that its key off is not lost to the next tick's own key off.
--out keeps the logs and the recording."""
import argparse
import os
import pathlib
import shutil
import struct
import sys
import tempfile

import numpy as np

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc/tools"))
sys.path.insert(0, str(ROOT / "pc/tests"))
from rood_rpc import OUT, symbols  # noqa: E402
from checkpoints import launch_at  # noqa: E402

CURE_ROOT = 323
MISC = 0xE00  # vs_main_inventory: the misc bag (id u16, count u8, index u8)
CLOSED = 0x3F
STREAM = 0x80039B08  # g_Sound_Cutscene_StreamState
STREAM_VOICES, STREAM_LEFT = STREAM + 0x0C, STREAM + 0x14  # VoicesInUseFlags, TotalPages (bytes still to load)
RATE = 44100
LEVEL_RATIO, REPEAT_LIMIT = 1.3, 0.4


def card_default():
    env = os.environ.get("ROOD_TEST_CARD")
    return pathlib.Path(env) if env else ROOT / "pc/build/test-card1.mcd"


def samples(wav):
    """The recording as mono floats (the header's length is only updated once a second: read to the end)."""
    raw = np.fromfile(wav, dtype=np.int16, offset=44)
    return raw[: len(raw) // 2 * 2].reshape(-1, 2).astype(np.float64).mean(axis=1)


def rms(x):
    return float(np.sqrt((x ** 2).mean()))


def repeat_peak(x):
    """Largest normalised autocorrelation over lags 50 ms..1 s, and its lag in samples."""
    n = len(x)
    x = x - x.mean()
    f = np.fft.rfft(x, 2 * n)
    r = np.fft.irfft(f * np.conj(f))[:n]
    r /= r[0]
    lo, hi = int(0.05 * RATE), int(1.0 * RATE)
    k = lo + int(np.argmax(r[lo:hi]))
    return float(r[k]), k


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--card", default=str(card_default()))
    parser.add_argument("--out", help="keep the logs here")
    parser.add_argument("--no-irq", action="store_true",
                        help="ROOD_SPU_IRQ=0: the interrupt's callback never runs, so the stream loops until "
                             "the driver's stop command, whose key off (written between two driver ticks) must "
                             "still reach the voices")
    args = parser.parse_args()
    if not pathlib.Path(args.card).exists():
        print("SKIP: no memory card", args.card)
        return 77
    out = (pathlib.Path(args.out) if args.out else OUT / "heal_audio").resolve()
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True, exist_ok=True)
    card = out / "card1.mcd"
    shutil.copyfile(args.card, card)
    lights = pathlib.Path(tempfile.mkdtemp(prefix="lights-", dir=out))
    env = {"ROOD_LIGHTS": str(lights), "ROOD_PREFABS": str(out / "prefabs"),
           "ROOD_LIGHTING": str(out / "lighting"), "ROOD_GI": "0"}
    if args.no_irq:
        env["ROOD_SPU_IRQ"] = "0"
    (out / "prefabs").mkdir()
    (out / "lighting").mkdir()
    if not os.environ.get("ROOD_OUT"):  # Continue reads the card only with ROOD_OUT set (run.py sets it)
        env["ROOD_OUT"] = str(out)
    os.environ.pop("ROOD_SETTINGS", None)
    socket = os.environ.get("ROOD_RPC") or f"/tmp/rood-heal-{os.getpid()}.sock"
    wav = out / "audio.wav"
    failures = []

    def check(ok, message):
        print(("ok:   " if ok else "FAIL: ") + message, flush=True)
        if not ok:
            failures.append(message)
        return ok

    g = launch_at("save_room", card=card, fast=True, log=out / "game.log", extra_env=env, socket_path=socket,
                  audio=f"wav:{wav}")
    sym = symbols()
    try:
        def ui():
            return g.call("ui_menu")

        def mode_now():
            return g.read(sym["D_800F196C"], 1)[0], g.read(sym["vs_battle_menuState"], 1)[0]

        def wait(pred, limit, what, step=2):
            for _ in range(0, limit + 1, step):
                if pred():
                    return
                g.run(step)
            raise TimeoutError(f"{what}: not after {limit} vsyncs (mode {mode_now()})")

        def recorded():
            return g.call("system")["audio_recorded"]

        def stream():
            return struct.unpack("<i", g.read(STREAM_VOICES, 4))[0]

        def left():
            return struct.unpack("<I", g.read(STREAM_LEFT, 4))[0]

        def roots():
            raw = g.read(sym["vs_main_inventory"] + MISC, 64 * 4)
            return sum(raw[k * 4 + 2] for k in range(64) if struct.unpack_from("<H", raw, k * 4)[0] == CURE_ROOT)

        g.call("ui_menu", modern=True)
        g.call("skip_charging", mode="off")
        g.run(60)
        base_from = recorded()
        g.run(300)
        base_to = recorded()
        before = roots()
        spu_before = g.call("spu")
        check(stream() == 0, "no stream before the heal")
        # Items > Misc, the first row (a Cure Root stack), Use, on Ashley.
        for _ in range(6):
            if ui().get("visible") and mode_now()[1] == 0:
                break
            g.press("triangle", vsyncs=3, settle=4)
            try:
                wait(lambda: ui().get("visible") and mode_now()[1] == 0, 120, "the menu")
            except TimeoutError:
                pass
            g.run(6)
        for _ in range(8):
            if ui().get("tab") == "items":
                break
            g.press("r1", vsyncs=3, settle=4)
        for _ in range(8):
            if ui().get("items", {}).get("category") == "misc":
                break
            g.press("r2", vsyncs=3, settle=4)
        g.press("circle", vsyncs=3, settle=4)
        for _ in range(10):
            m = ui()["items"]
            if m.get("mode") == "actions" and m["actions"][m["action"]]["id"] == "use":
                break
            g.press("down", vsyncs=3, settle=4)
        g.press("circle", vsyncs=3, settle=1)
        wait(lambda: mode_now() == (2, CLOSED), 300, "the target")
        g.run(20)
        for _ in range(6):  # the target cursor takes presses once it is up
            g.call("input", buttons=["circle"], vsyncs=3)
            g.run(4)
            if g.read(sym["_cameraMode"], 1)[0] not in (1, 3):
                break
            g.run(36)
        # The action, polled for the stream; then back in the field.
        streamed, ended_at, lefts = 0, None, []
        for n in range(0, 1200, 4):
            s = stream()
            streamed |= s
            if s:
                lefts.append(left())
            if streamed and not s and ended_at is None:
                ended_at = n
            if n >= 400 and mode_now()[0] == 1:
                break
            g.run(4)
        wait(lambda: mode_now()[0] == 1, 600, "back in the field", step=10)
        field = recorded()
        check(roots() == before - 1, f"the Cure Root was used ({before} -> {roots()})")
        check(streamed != 0, f"its sound was streamed (voices {streamed:06x})")
        loads = sorted(set(lefts), reverse=True)
        check(len(loads) >= 3 or args.no_irq,
              f"the stream was fed by the SPU interrupt{' (not served: --no-irq)' if args.no_irq else ''}: "
              f"{len(loads)} parts loaded "
              f"(bytes left {', '.join(hex(v) for v in loads[:8])}{', ...' if len(loads) > 8 else ''})")
        check(ended_at is not None and stream() == 0,
              f"the driver ended the stream (VoicesInUseFlags 0"
              + (f", {ended_at} vsyncs after the action started)" if ended_at is not None
                 else f"; still {stream():06x} in use)"))
        g.run(120)
        spu = g.call("spu")
        check(not (spu["keyed"] | spu["sounding"]) & streamed,
              f"the stream's voices are keyed off and silent 2 s after the field is back "
              f"(keyed {spu['keyed']:06x}, sounding {spu['sounding']:06x}, stream {streamed:06x})")
        check(not spu["irq"]["enabled"] and not spu["reverb_voices"] & streamed
              and spu["reverb"] == spu_before["reverb"],
              f"the SPU interrupt is off and the reverb is as before the heal (irq {spu['irq']}, "
              f"master {spu_before['reverb']} -> {spu['reverb']}, sends {spu_before['reverb_voices']:06x} -> "
              f"{spu['reverb_voices']:06x})")
        g.run(600)
        log = pathlib.Path(g.log).read_text(errors="replace")
        check("[UNIMPLEMENTED]" not in log and "[crash]" not in log, "no crash")
    finally:
        g.close()

    x = samples(wav)
    base = x[base_from:base_to]
    after = x[field + 2 * RATE: field + 10 * RATE]
    if not check(len(after) == 8 * RATE, f"recorded 10 s after the field is back ({len(x) - field} frames)"):
        return 1
    level, room = rms(after), rms(base)
    check(level <= LEVEL_RATIO * room,
          f"the level 2-10 s after the heal is the room's: RMS {level:.0f} vs {room:.0f} before "
          f"({level / room:.2f}x, <= {LEVEL_RATIO}x)")
    peak, lag = repeat_peak(after)
    base_peak, _ = repeat_peak(base)
    check(peak < REPEAT_LIMIT,
          f"nothing repeats after the heal: autocorrelation {peak:.2f} at {lag} samples (< {REPEAT_LIMIT}; "
          f"the room before: {base_peak:.2f})")
    if failures:
        print(f"FAIL: {len(failures)} check(s)")
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
