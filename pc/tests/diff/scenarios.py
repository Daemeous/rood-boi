"""Deterministic corpus recipes, not claims of gameplay coverage.

Only enemy_cast_wait is implemented. It reuses the real-room setup of
shadow_battle_smoke; routed calls must actually occur for a run to pass.
"""
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
        game.call("skip_charging", mode=0, reset=True)

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


SCENARIOS = {"enemy_cast_wait": Scenario("enemy_cast_wait")}
for _name in ("spell", "item", "attack", "chain", "cancel", "miss", "defence",
              "break_art", "status", "multiple_targets", "enemy_kill", "roaming",
              "doors", "events", "cutscene", "menus_hud", "effects", "traps",
              "loot", "save_point", "actor_transform", "action_name_window"):
    SCENARIOS[_name] = Scenario(_name, incomplete="classic-input recipe and semantic witness not implemented")


def select(names=None):
    """Default means the whole required corpus, including explicit gaps."""
    return [SCENARIOS[name] for name in (names or SCENARIOS)]
