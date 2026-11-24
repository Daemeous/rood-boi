/* The enemy difficulty option (pc/docs/Enemy_Difficulty.md): its own unit, which the
 * Zig battle code reaches through the wrappers at the end (declared in
 * pc/src/game/BATTLE/BATTLE.PRG/mod/hooks.h). */
#include "common.h"
#include "pc/src/game/BATTLE/BATTLE.PRG/146C.h"
#include "pc/src/game/BATTLE/BATTLE.PRG/mod/hooks.h"
#include "pc/src/game/SLUS_010.40/main.h"
#include "enemy_difficulty.inc"

void pc_difficulty_scene_now(void)
{
    pc_difficulty_begin_scene((unsigned char)vs_main_stateFlags.clearCount);
}

void pc_difficulty_store(void* record, const void* stats)
{
    pc_difficulty_cache_store(record, (const vs_battle_actor2*)stats);
}

void pc_difficulty_init(int slot, const void* init, void* stats)
{
    pc_difficulty_init_actor(slot, (unsigned char)((const vs_battle_charInitData*)init)->characterKind, (vs_battle_actor2*)stats);
}

void pc_difficulty_restore(const void* record, int slot, const void* init, void* stats)
{
    pc_difficulty_cache_restore(record, slot, (unsigned char)((const vs_battle_charInitData*)init)->characterKind,
        (vs_battle_actor2*)stats);
}
