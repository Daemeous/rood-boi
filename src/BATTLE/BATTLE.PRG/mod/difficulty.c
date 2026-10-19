/* Optional PS1 patch-edition module, loaded into a reserved 16 KB heap prefix.
 * It keeps memory-card room records in vanilla stat units, as the PC version does. */
#ifndef ROOD_PC
#include "common.h"
#include "../146C.h"
#include "difficulty_runtime.h"
void func_80076784(int, vs_battle_actor2*, vs_battle_charInitData*, int);
void func_80069DBC(void);
void* func_80069E80(int);

void vs_diff_start(void)
{
    unsigned int* header = (unsigned int*)0x8010C000;
    vs_diff_base_percent = header[3];
    vs_diff_clear_percent = header[4];
    vs_diff_begin_scene((unsigned char)vs_main_stateFlags.clearCount);
}
void vs_diff_init(int slot, vs_battle_actor2* actor, vs_battle_charInitData* init, int random)
{
    func_80076784(slot, actor, init, random);
    vs_diff_init_actor(slot, (unsigned char)init->unk2, actor);
}
void vs_diff_clear(void)
{
    func_80069DBC();
    vs_diff_begin_scene((unsigned char)vs_main_stateFlags.clearCount);
}
void vs_diff_save(int slot, int flags)
{
    vs_battle_actor* actor;
    void* saved;
    func_80069FC4(slot, flags);
    actor = vs_battle_actors[slot];
    if (actor == 0 || actor->unk27 == 0x80) return;
    saved = func_80069E80(actor->unk27);
    if (saved != 0) vs_diff_cache_store(saved, actor->unk3C);
}
void vs_diff_restore(int statuses, vs_battle_actor2* actor)
{
    int slot = actor->unk957;
    vs_battle_actor* outer = vs_battle_actors[slot];
    void* saved = func_80069E80(outer->unk27);
    if (saved != 0) vs_diff_cache_restore(saved, slot, (unsigned char)outer->unk18, actor);
    func_80086FA8(statuses, actor);
}
#endif
