//! Battle slot bookkeeping: marks a slot's partner and walks the slot's object list (src/BATTLE/BATTLE.PRG/30DB0.c).
const psx = @import("common").psx;
const v = @import("views_30DB0");
const c = @import("c_30DB0");

/// orig: func_8009F530 (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Flags slot `slot`, finds its partner slot and updates the candidates, then clears the objects listed under the slot.
pub fn func_8009F530(slot: c_int) callconv(.c) void {
    const owner: *v.D_800F45E0_t = psx.at(v.g.D_800F45E0(), slot).*.?;
    psx.setBits(&owner.unk6C[8], "unk0_3", 1);
    const listSlot: c_int = c.func_800A91DC(@as(c_int, owner.unk5C), @as(c_int, owner.unk5E), 0);
    const partner: c_int = c.func_8009F794(@ptrCast(owner));

    if (partner != -1) {
        const target: *v.D_800F45E0_t = psx.at(v.g.D_800F45E0(), partner).*.?;
        target.unk36 = 0;
        target.unk1A = 0xFC;
        c.func_800E68A0(@ptrCast(target));

        if (psx.getBits(&target.unk6C[8], "unk0_4") == 2) {
            c.func_800E68A0(@ptrCast(target));
        }

        var k: c_int = 0;
        while (k < 16) : (k +%= 1) {
            // k < 16: the slot count.
            if (psx.at(v.g.D_800F45E0(), k).*) |other| {
                if (other != target and psx.getBits(other, "unk8_0") == 0) {
                    other.unk6C[8].unk3 = 0xFF;
                    if (other.unk1A == 0 and
                        psx.getBits(&other.unk6C[8], "unk0_3") == 0 and
                        other.unk12 == 0xFF and
                        other.unk5C == target.unk5C and
                        other.unk5D == target.unk5D and
                        other.unk5E == target.unk5E and
                        other.unk1E <= target.unk1E)
                    {
                        other.unk6C[8].unk3 = psx.conv(u8, partner);
                    }
                }
            }
        }
    }

    var i: c_int = 0;
    while (true) {
        const found_raw = c.func_800A3C00(@ptrCast(psx.at(v.g.D_800F45E0(), listSlot).*), psx.conv(c_uint, i));
        const found: ?*v.D_800F4538_t = @ptrCast(@alignCast(found_raw));
        const entry = found orelse break;

        if (psx.getBits(&entry.unk0, "unkA_0") != 3) {
            @as(*align(1) c_int, @ptrCast(&entry.unk0.unk34)).* = 0;
            @as(*align(1) c_int, @ptrCast(&entry.unk0.unk34.vz)).* = 0;
            psx.setBits(&entry.unk0, "unkA_0", 3);
            if (entry.unk0.unkF != 0) {
                entry.unk181A = 1;
                entry.unk1814 = psx.conv(i16, entry.unk0.currentTileX);
                entry.unk1818 = psx.conv(i16, entry.unk0.currentTileZ);
            }
            entry.unk0.unk11 = 4;
        }

        i = @as(c_int, entry.unk0.unkF) +% 1;
        psx.setBits(&entry.unk0, "unkA_3", 0);
        psx.setBits(&entry.unk0, "unk9_6", 0);
        psx.setBits(entry, "unk5AC_28", 0);

        if (i == 1) {
            i = 2;
        }
    }
}
