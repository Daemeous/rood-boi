//! Spawns an actor into an empty battle slot (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_actors_queueScriptedActorSpawn (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Fills empty battle slot `arg0` with a new actor record and runs its setup, returning 1 plus that setup's result, or 0 if the slot is taken or out of range.
pub fn vs_actors_queueScriptedActorSpawn(arg0: c_uint, arg1: c_int, arg2: ?*v.vs_battle_objectData_flags) callconv(.c) c_int {
    if (arg0 < 16) {
        const slots = v.g.vs_battle_actors();
        const slot: *?*v.vs_battle_actor = &slots[@as(usize, arg0)];
        if (slot.* == null) {
            const temp: *v.vs_battle_actor_dat = @ptrCast(@alignCast(c.vs_main_allocHeap(@sizeOf(v.vs_battle_actor_dat)).?));
            const actor: *v.vs_battle_actor = &temp.unk0.unk0.unk0;
            slot.* = actor;
            psx.put(&actor.stats, &temp.unk0.unk0.unk50);
            actor.unk40 = 1;
            psx.put(&actor.unk44, &temp.unk0.unk9B4);
            psx.put(&actor.unk48[0], &temp.unk22B4[0]);
            psx.put(&actor.unk48[1], &temp.unk22B4[1]);
            c.vs_actors_initializeActorDefaults(arg0, arg1, 0, 0, 6);
            const s0 = c.vs_actors_queueActorModelLoad(@ptrCast(actor), psx.conv(c_int, arg0), arg1, @ptrCast(arg2), 0);
            _ = c.vs_battle_aiCreateActorAiState(@ptrCast(slot.*), -1);
            return s0 +% 1;
        }
    }
    return 0;
}
