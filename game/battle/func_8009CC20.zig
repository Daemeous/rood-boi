//! Battle object-sequence and slot bookkeeping: frees a actor's sequence or a
//! shared slot (src/BATTLE/BATTLE.PRG/30DB0.c).
const psx = @import("common").psx;
const v = @import("views_30DB0");
const c = @import("c_30DB0");

/// orig: vs_battleFindSharedSequence (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Finds another actor slot whose sequence can be shared with `data`, and records its index in `data.dataAddr`.
fn findSharedSequence(data: *v.vs_battle_objectData) bool {
    const actors = v.g.D_800F4538();
    const t1: c_int = @as(c_int, psx.at(actors, data.index).*.?.unk6E6);
    const start: c_int = if (@as(c_int, data.index) >= 2) 2 else 0;
    var a: c_int = start;
    while (a < 17) : (a += 1) {
        if (a == @as(c_int, data.index)) continue;
        const temp = actors[@intCast(a)] orelse continue;
        if (@as(c_int, temp.unk6E6) != t1) continue;
        const sameModel = @as(c_int, data.modelId) == 0 and psx.get(temp.unk5D4) != null;
        const sameId = @as(c_int, data.modelId) != 0 and @as(c_int, temp.unk5B1) == @as(c_int, data.modelId);
        if (sameModel or sameId) {
            data.dataAddr = psx.conv(u32, a);
            return true;
        }
    }
    return false;
}

/// orig: func_8009CC20 (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Frees a slot's object sequence (op 0), a per-slot sequence pointer across actors (ops 1, 2), or an actor's fourth sequence (ops 4, 5).
pub fn func_8009CC20(slot: c_int, op: c_int) callconv(.c) c_int {
    switch (op) {
        0 => {
            if (slot >= 17) return -1;
            const actor = psx.at(v.g.D_800F4538(), slot).* orelse return -1;
            if (psx.get(actor.unk5D8) == null) return 0;
            if (slot != 0) {
                var objectData: v.vs_battle_objectData = undefined;
                objectData.index = psx.conv(i8, slot);
                objectData.modelId = psx.conv(i16, @as(c_int, actor.unk5B1));
                if (slot == 1 and actor.unk6E6 == 0) {
                    c.vs_main_freeHeap(psx.get(actor.unk5D8));
                } else if (!findSharedSequence(&objectData)) {
                    c.vs_main_freeHeap(psx.get(actor.unk5D8));
                }
            }
            psx.put(&actor.unk5D8, null);
            actor.unk5B1 = 0;
            actor.unk5B5 = 0;
        },
        1, 2 => {
            // The C decrements the parameter (wrapping); the index is unchecked, as the C is.
            const index = slot -% 1;
            var i: usize = 0;
            while (i < 17) : (i += 1) {
                if (v.g.D_800F4538()[i]) |actor| {
                    psx.put(psx.at(&actor.unk5DC, index), null);
                }
            }
            const ret = c.func_8009D208(index +% 1);
            if (ret < 0) return ret;
            const slotPtr = psx.at(v.g.D_800F4528(), index);
            if (slotPtr.* != null) c.vs_main_freeHeap(slotPtr.*);
            slotPtr.* = null;
        },
        4, 5 => {
            const actor = psx.at(v.g.D_800F4538(), slot).* orelse return -1;
            if (psx.get(actor.unk5DC[3]) == null) return 0;
            c.vs_main_freeHeap(psx.get(actor.unk5DC[3]));
            psx.put(&actor.unk5DC[3], null);
            actor.unk5B6 = 0;
        },
        else => {},
    }
    return 0;
}
