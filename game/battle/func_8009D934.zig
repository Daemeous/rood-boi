//! Weapon draw and sheathe for a battle entity (src/BATTLE/BATTLE.PRG/30DB0.c).
const psx = @import("common").psx;
const v = @import("views_30DB0");
const c = @import("c_30DB0");

/// orig: func_8009D934 (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Draws (arg1 != 0) or sheathes entity arg0's weapon; mode 2 only sets its weapon models.
pub fn func_8009D934(arg0: c_int, arg1: c_int, arg2: c_int) callconv(.c) void {
    const entity = psx.at(v.g.D_800F4538(), arg0).* orelse return;

    if (arg2 == 2) {
        psx.setBits(&entity.unk0, "weaponDrawn", psx.conv(u1, arg1));
        var i: c_int = 0;
        while (i < 2) : (i += 1) {
            const model = psx.at(v.g.vs_battle_wepModels(), arg0 *% 2 +% i).* orelse continue;
            if (arg1 != 0) {
                model.unk11 = 0x40;
                model.unk12 = 0x40;
                psx.setBits(model, "unk8_4", 1);
            } else {
                model.unk11 = 0;
                model.unk12 = 0;
                psx.setBits(model, "unk8_4", 0);
            }
        }
        return;
    }

    var var_s2: c_int = 5;
    var temp_s1: c_int = undefined;
    if (arg2 != 0) {
        var_s2 = 16;
        temp_s1 = @as(c_int, v.g.D_800E9090()[@as(usize, entity.unk5B1)]);
    } else {
        temp_s1 = @as(c_int, v.g.D_800E909C()[@as(usize, entity.unk5B1)]);
    }

    if (@as(c_int, c.func_800BEB00()) == 4) {
        psx.setBits(&entity.unk0, "weaponDrawn", psx.conv(u1, arg1));
        _ = c.func_800A0204(arg0, 1, 0, 16);
        return;
    }

    if (arg1 != @as(c_int, psx.getBits(&entity.unk0, "weaponDrawn"))) {
        if (arg1 == 0) {
            if (entity.unk6E6 == 0) {
                c.func_800AE6C0(@ptrCast(entity), temp_s1 +% 1, 0);
            }

            _ = c.func_800A0204(arg0, arg2 *% 2 +% 10, 0, var_s2);

            psx.setBits(entity, "unk5AC_20", 1);
            psx.setBits(&entity.unk0, "weaponDrawn", 0);

            _ = c.func_800A01C8(arg0, 1, 16, 0);
        } else {
            if (entity.unk6E6 == 0) {
                c.func_800AE6C0(@ptrCast(entity), temp_s1, 0);
            }

            psx.setBits(&entity.unk0, "weaponDrawn", psx.conv(u1, arg1));

            _ = c.func_800A0204(arg0, arg2 *% 2 +% 9, 0, var_s2);

            psx.setBits(entity, "unk5AC_20", 1);

            _ = c.func_800A01C8(arg0, 1, 16, 0);
        }
    }
}
