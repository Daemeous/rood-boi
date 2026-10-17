"""The original top level by the game's own cursor (vs_battle_submenuStates[0]),
not by counting rows: which commands it shows varies (a spell learned, fast
travel's Magic command on a Circle of Magic, pc/docs/Fast_Travel.md)."""
# the commands' rows (currentMenu = row + 1: Data is 7)
MAGIC_COMMAND, ABILITIES_COMMAND, MAP_COMMAND, DATA_COMMAND = 0, 2, 5, 6


def top_command(game, sym, command, limit=16):
    """Down until the top level's cursor is on `command` (the menu open)."""
    for _ in range(limit):
        if game.read(sym["vs_battle_submenuStates"], 1)[0] == command:
            return
        game.press("down", vsyncs=3, settle=10)
    raise TimeoutError(f"the top level's cursor never reached command {command} "
                       f"(at {game.read(sym['vs_battle_submenuStates'], 1)[0]})")
