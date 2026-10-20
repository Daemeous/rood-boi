"""Local-only corpus artifacts and the classic oracle profile."""
import hashlib
import json
import pathlib
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[3]
CACHE = ROOT / "pc/build/diff-corpus"
# Disable feature gates, not safety/LP64 adaptations or original gameplay.
OFF = """widescreen interpolate interp_skeleton interp_holds interp_uv
perspective_textures subpixel smooth_textures ui_subpixel ui_outline_boost
native_room native_occlusion native_room_order room_texture_lod native_actors
native_actor_order native_blend native_objects native_shadows native_effects
reconstruct baked_estimate probe_show light_animation environment emissive fog
camera_stick camera_stick_keys camera_stick_zoom qol_map_invert qol_map_controls
qol_fp_invert_y qol_confirm_cross qol_field_cross_attack qol_weapon_slots
qol_skip_charging qol_fast_travel qol_fast_travel_prompt skip_intro ui_menus""".split()
PROFILE = "".join(f"{key} 0\n" for key in OFF)
# Feature-gated ROOD_PC hooks are outside the classic profile (a port that
# dropped pc_charge_skip_name(1) passed it): the features profile turns on
# the features whose hooks run in game code and change state the oracle
# compares, keeping off the ones that change what a recipe's input means
# (Cross as confirm or attack, the weapon slots, modern menus, the camera
# stick, the map controls) and the renderer's own.
FEATURES_ON = ["qol_skip_charging"]
# ... and for every caster (the enemies' wind-ups too: the corpus recipes
# watch enemies cast).
FEATURES_SETTINGS = {"qol_skip_charging_who": 1}
FEATURES = "".join(f"{key} {1 if key in FEATURES_ON else 0}\n" for key in OFF) + \
    "".join(f"{key} {value}\n" for key, value in FEATURES_SETTINGS.items())
PROFILES = {"classic": PROFILE, "features": FEATURES}


def cache_path(path):
    """Even --out must stay inside ignored build cache, including symlinks."""
    path = pathlib.Path(path).resolve()
    if not path.is_relative_to(CACHE.resolve()):
        raise ValueError(f"corpus output must be under {CACHE}")
    return path


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def write_json(path, value):
    pathlib.Path(path).write_text(json.dumps(value, indent=2, sort_keys=True) + "\n")


def environment(work, subsystem, profile="classic"):
    settings = work / "settings.cfg"
    settings.write_text(PROFILES[profile])
    for name in ("lights", "prefabs"):
        (work / name).mkdir(exist_ok=True)
    return {"ROOD_ORACLE": "1", "ROOD_DETERMINISTIC": "1", "ROOD_GI": "0",
            "ROOD_OWNER": f"{subsystem}=original", "ROOD_SETTINGS": str(settings),
            "ROOD_LIGHTS": str(work / "lights"), "ROOD_PREFABS": str(work / "prefabs")}


def clock_mask(binary):
    """Only boot_runtime.c's verified host pacing clock, never same-name locals."""
    script = ("import gdb,json; v=gdb.parse_and_eval(\"'boot_runtime.c'::last_vsync\"); "
              "t=v.type.strip_typedefs(); "
              "assert t.tag == 'timespec' and [f.name for f in t.fields()] == ['tv_sec','tv_nsec']; "
              "print('ORACLE_CLOCK='+json.dumps([int(v.address),int(t.sizeof)]))")
    result = subprocess.run(["gdb", "-nx", "-nh", "-batch", "-iex", "set auto-load off",
                             str(binary), "-ex", "python " + script], capture_output=True,
                            text=True, timeout=30, check=True)
    rows = [json.loads(line.split('=', 1)[1]) for line in result.stdout.splitlines()
            if line.startswith('ORACLE_CLOCK=')]
    if len(rows) != 1 or rows[0][0] <= 0 or rows[0][1] not in (8, 16):
        raise RuntimeError("cannot verify boot_runtime.c host timespec mask")
    return rows


def generate(game, scenario, work, subsystem, masks=()):
    """Generate once in the already launched process; never relaunch to replay."""
    game.owner(subsystem, "original")
    scenario.setup(game)
    if game.call("ui_menu").get("visible"):
        raise RuntimeError("modern menu open at mark")
    game.replay(None)
    scenario.reset_witness(game)
    game.mark()
    snapshot = work / "start.snap"
    game.call("snapshot", action="save", path=str(snapshot))
    recording = work / "inputs.rec"
    game.digest("stop")
    game.digest("record", ticks=scenario.ticks, masks=list(masks))
    game.record(recording)
    try:
        scenario.play(game)
    finally:
        game.record(None)
    result = game.digest("stop")
    if result.get("error") or result["recorded"] != scenario.ticks:
        raise RuntimeError(f"incomplete oracle recording: {result}")
    if game.call("ui_menu").get("visible"):
        raise RuntimeError("modern menu open after recording")
    witness = scenario.witness(game)
    write_json(work / "oracle-witness.json", witness)
    if not witness["ok"]:
        raise RuntimeError(f"oracle semantic witness failed: {witness}")
    manifest = {"schema": 1, "scenario": scenario.name, "ticks": result["recorded"],
                "snapshot_sha256": sha256(snapshot), "inputs_sha256": sha256(recording),
                "profile_sha256": sha256(work / "settings.cfg"), "masks": list(masks),
                "mask_reason": "boot_runtime.c host pacing timespec, verified by qualified DWARF",
                "replay_contract": "same-process mark/rewind only"}
    write_json(work / "manifest.json", manifest)
    return recording, result["recorded"]
