"""Deterministic corpus recipes, not claims of gameplay coverage.

Implemented: enemy_cast_wait (the real-room setup of shadow_battle_smoke),
action_name_window (the same room's casts, witnessed by the exact text of the
three name windows they open) and enemy_kill (Ashley kills two hostile
enemies with her weapon, witnessed by the exact score changes). Routed calls
must actually occur for a run to pass.
"""
import os
import struct
from dataclasses import dataclass


@dataclass(frozen=True)
class Scenario:
    name: str
    ticks: int = 600
    room: tuple = (48, 10)
    incomplete: str = ""

    def setup(self, game):
        if self.incomplete:
            raise NotImplementedError(self.incomplete)
        game.call("room_goto", zone=self.room[0], room=self.room[1],
                  enemies=True, scripts=False)
        for _ in range(80):
            game.run(20)
            if game.call("room_goto", action="status")["phase"] in (0, 4):
                break
        else:
            raise TimeoutError("room transition did not settle")
        state = game.state()
        if (state["zone"], state["room"]) != self.room:
            raise RuntimeError("wrong scenario room")

    def reset_witness(self, game):
        # Counters only: the charging skip's mode is the profile's (off in the
        # classic profile, on in the features one: corpus.py).
        game.call("skip_charging", reset=True)

    def witness(self, game):
        stats = game.call("skip_charging")
        ended = stats.get("ended", "")
        entries = [entry.split(":") for entry in ended.split()]
        casts = [entry for entry in entries if len(entry) >= 4
                 and int(entry[0]) != 0 and entry[2] == "1"]
        return {"ok": bool(casts), "ended": ended, "enemy_casts": casts}

    def play(self, game):
        # Recording includes neutral frames; no modern-menu/RPC play shortcuts.
        game.run(self.ticks)


@dataclass(frozen=True)
class NameWindowScenario(Scenario):
    """The enemies' casts in enemy_cast_wait's room over 1200 vsyncs, witnessed
    by the action name windows they open (vs_battle_displaySceneMessage, three
    calls; the charging tracker records each window's action and text,
    pc/src/charge_skip.inc). Exact: the three windows, in order, each with the
    text the original writes (the 0x04FB00F8 header, the action type's label
    and the action's name), the same in the classic and features profiles."""
    ticks: int = 1200
    EXPECTED = ("54:f800fb041c33282f2f9e8f910d282a2831283524372891",
                "95:f800fb041c33282f2f9e8f910d35242c31fa06162c312791",
                "75:f800fb041c33282f2f9e8f911c2c2f2831262891")

    def witness(self, game):
        stats = game.call("skip_charging")
        names = [entry for entry in stats.get("name", {}).get("names", "").split()
                 if not entry.startswith("-")]
        return {"ok": tuple(names) == self.EXPECTED, "name_windows": names, "expected": list(self.EXPECTED),
                "ended": stats.get("ended", "")}


def _pointer_size():
    return 8 if os.environ.get("ROOD_ARCH") == "64" else 4


@dataclass(frozen=True)
class KillScenario(Scenario):
    """Ashley kills two hostile enemies with her weapon, classic input only:
    _updateEnemyKills runs twice, on two branches, for odd enemy classes.

    Setup (before the mark, so in the pinned state): zone 9 room 18 with its
    enemies, the weapon drawn (Circle), 600 vsyncs for the nearer Zombie to
    come, 50 vsyncs of Right+Down, then every hostile enemy's HP set to 1
    (vs_battle_actor2.currentHP), so one hit kills. The farther Zombie (it
    idles out of reach) is moved to (340, 0, 702) beside the nearer one, the
    first hostile gets enemyClass 1 (Beast) and the second 3 (Phantom): odd
    classes, whose enemyKills entries sit at 2 mod 4 (the psx.at alignment
    case). The streak is set to class 1, count 99, and the streak score to 0.
    Play (recorded): four times Circle (the targeting sphere), Circle (the
    body part list), Circle (attack) and 350 vsyncs. The moved Zombie dies
    first (class 1: the same class as the streak, the count 99 -> 100), then
    the other (class 3: a new class, the count back to 0).
    Witness, exact: enemyKills[1] and [3] up by one and no other entry
    changed; the streak class 3, count 0; the streak score 0 + 154 + 154*100/2
    + 154*1/2 (the weapon streak) + 0 (chain) = 7931, then + 133 + 0 + 133*2/2
    = 8197; both enemies' kill flags set."""
    room: tuple = (9, 18)
    ticks: int = 1700
    HP = 0x18  # vs_battle_actor2 currentHP
    CLASS = 55  # vs_battle_actor2 enemyClass (bits 0-2)
    STATS = 0x3C  # vs_battle_actor unk3C (stats)
    SUBTYPE = 0x1A  # vs_battle_actor subType
    ALLEGIANCE = 0x1C  # vs_battle_actor unk1C: 4 hostile
    POSITION = 28  # D_800F4538_t unk0.position
    ENEMY_KILLS = 4  # vs_main_scoredata_t enemyKills[6]
    KILL_FLAGS = 0x9C  # vs_main_scoredata_t enemyKillFlags[26]
    STREAK_SCORE = 0x104  # vs_main_scoredata_t streakScore
    STREAK = 0x10C  # killStreakEnemyClass, killStreakEnemyCount
    CLASSES = (1, 3)
    EXPECTED_SCORE = 8197

    def setup(self, game):
        super().setup(game)
        game.press("circle", 3, 30)
        game.run(600)
        game.hold(["right", "down"], 50)
        from rood_rpc import symbols
        sym, width = symbols(), _pointer_size()
        hostiles = []
        for slot in range(1, 16):
            actor = int.from_bytes(game.read(sym["vs_battle_actors"] + slot * width, width), "little")
            if not actor:
                continue
            stats = int.from_bytes(game.read(actor + self.STATS, 4), "little")
            hostile = struct.unpack("<H", game.read(actor + self.ALLEGIANCE, 2))[0] == 4
            if stats and hostile:
                game.write(stats + self.HP, struct.pack("<h", 1))
                hostiles.append((slot, actor, stats))
        if len(hostiles) != 2:
            raise RuntimeError(f"expected two hostile enemies, found {len(hostiles)}")
        for (slot, actor, stats), enemy_class in zip(hostiles, self.CLASSES):
            byte = game.read(stats + self.CLASS, 1)[0]
            game.write(stats + self.CLASS, bytes([(byte & ~7) | enemy_class]))
        moved = int.from_bytes(game.read(sym["D_800F4538"] + hostiles[0][0] * width, width), "little")
        game.write(moved + self.POSITION, struct.pack("<3h", 340, 0, 702))
        score = sym["vs_main_scoredata"]
        game.write(score + self.STREAK, bytes([self.CLASSES[0], 99]))
        game.write(score + self.STREAK_SCORE, struct.pack("<I", 0))
        object.__setattr__(self, "_subtypes", [struct.unpack("<H", game.read(actor + self.SUBTYPE, 2))[0]
                                               for _, actor, _ in hostiles])

    def score(self, game):
        from rood_rpc import symbols
        data = game.read(symbols()["vs_main_scoredata"], 0x110)
        return {"kills": list(struct.unpack_from("<6H", data, self.ENEMY_KILLS)),
                "flags": list(struct.unpack_from("<26I", data, self.KILL_FLAGS)),
                "streak": list(data[self.STREAK:self.STREAK + 2]),
                "streak_score": struct.unpack_from("<I", data, self.STREAK_SCORE)[0]}

    def reset_witness(self, game):
        object.__setattr__(self, "_baseline", self.score(game))

    def witness(self, game):
        before, after = getattr(self, "_baseline", None), self.score(game)
        if before is None:
            return {"ok": False, "reason": "no baseline"}
        expected = list(before["kills"])
        for enemy_class in self.CLASSES:
            expected[enemy_class] += 1
        flags = all(after["flags"][t // 32] >> (t % 32) & 1 for t in getattr(self, "_subtypes", []) if t < 256)
        ok = (after["kills"] == expected and after["streak"] == [self.CLASSES[1], 0]
              and after["streak_score"] == self.EXPECTED_SCORE and flags)
        return {"ok": ok, "enemy_kills": [before["kills"], after["kills"]], "expected_kills": expected,
                "streak": after["streak"], "streak_score": after["streak_score"],
                "expected_streak_score": self.EXPECTED_SCORE, "kill_flags_set": flags}

    def play(self, game):
        used = 0
        for _ in range(4):
            for _ in range(3):
                game.press("circle", 3, 20)
                used += 23
            game.run(350)
            used += 350
        game.run(self.ticks - used)


SCENARIOS = {"enemy_cast_wait": Scenario("enemy_cast_wait"),
             "action_name_window": NameWindowScenario("action_name_window"),
             "enemy_kill": KillScenario("enemy_kill")}
for _name in ("spell", "item", "attack", "chain", "cancel", "miss", "defence",
              "break_art", "status", "multiple_targets", "roaming",
              "doors", "events", "cutscene", "menus_hud", "effects", "traps",
              "loot", "save_point", "actor_transform"):
    SCENARIOS[_name] = Scenario(_name, incomplete="classic-input recipe and semantic witness not implemented")
IMPLEMENTED = {name for name, scenario in SCENARIOS.items() if not scenario.incomplete}


def select(names=None):
    """Default means the whole required corpus, including explicit gaps."""
    return [SCENARIOS[name] for name in (names or SCENARIOS)]
