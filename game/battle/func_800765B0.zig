//! Creates an actor in a free slot (src/BATTLE/BATTLE.PRG/146C.c).
const std = @import("std");
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: func_800765B0 (src/BATTLE/BATTLE.PRG/146C.c)
/// Slot 0xFF takes the first free of 2..15 (none: vs_main_nop10(0x86) and null). An empty slot gets a cleared heap
/// actor (its stats and model block inside it, unk18 0xFE, allegiance 0x10), a model request (object data type 6,
/// the flags with unk0_8 cut to its low bit, variant from its high nibble, actorId and its high byte, the
/// material), waits for the object data queue, then is set up (func_8007647C, func_800E6178, func_800A087C).
/// Returns the new actor, or null for a slot already in use.
pub fn func_800765B0(slot: c_int, actorId: c_int, flags: ?*v.vs_battle_objectData_flags, material: c_int, _: c_int) callconv(.c) ?*v.vs_battle_actor {
    var index = slot;
    // The C's local leaves dataAddr uninitialised, and vs_battle_populateDataSlot copies all of it into the
    // slot: those bytes are 0 here (PORTING.md §11.4).
    var request = std.mem.zeroes(v.vs_battle_objectData);
    const actors = v.g.vs_battle_actors();
    if (index == 0xFF) {
        var i: c_int = 2;
        while (i < 16) : (i += 1) { // i < 16
            if (actors[@intCast(i)] == null) {
                index = i;
                break;
            }
        }
        if (i >= 16) {
            c.vs_main_nop10(0x86, 0);
            return null;
        }
    }
    // the slot is not bounded by the C
    if (psx.at(actors, index).* == null) {
        const block: *v.vs_battle_actor_dat2 = @ptrCast(@alignCast(c.vs_main_allocHeap(@sizeOf(v.vs_battle_actor_dat2))));
        c.vs_main_bzero(block, @sizeOf(v.vs_battle_actor_dat2_unk0));
        const actor = &block.unk0.unk0;
        psx.put(&actor.stats, &block.unk0.unk50);
        psx.put(&actor.unk44, &block.unk9B4);
        actor.unk18 = 0xFE;
        actor.allegiance = 0x10;
        actor.unk40 = 0;
        actor.id = index;
        psx.put(&actor.next, null);
        actor.unk26 = 0;
        request.dataType = 6;
        request.index = psx.conv(i8, index);
        request.unkC = flags.?.*;
        psx.setBits(&request.unkC, "unk0_8", psx.getBits(&request.unkC, "unk0_8") & 1);
        var high = actorId;
        psx.put(&request.unk4, @as(?*anyopaque, psx.get(actor.unk44)));
        request.actorId = psx.conv(u8, actorId);
        if (actorId < 0) {
            high = actorId +% 0xFF;
        }
        request.unk11 = psx.conv(u8, high >> 8);
        request.material = psx.conv(u8, material);
        request.variant = psx.getBits(flags.?, "unk0_8") >> 4;
        _ = c.vs_battle_populateDataSlot(@ptrCast(&request));
        while (c.vs_battle_getEmptyObjectDataSlot() != 0) {
            _ = c.vs_battle_processObjectDataQueue();
            _ = c.vs_main_gametimeUpdate(0);
        }
        psx.at(actors, index).* = actor;
        c.func_8007647C(index, actorId);
        _ = c.func_800E6178(@ptrCast(actor), -1);
        c.func_800A087C(index, 0x1846);
        return actor;
    }
    return null;
}
