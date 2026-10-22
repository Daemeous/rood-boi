//! Per-frame update of the battle unit's bone pose and its copy of the
//! previous pose (src/BATTLE/BATTLE.PRG/44F14.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_44F14");
const c = @import("c_44F14");

/// orig: func_800AF6E8 (src/BATTLE/BATTLE.PRG/44F14.c)
/// Advances the unit's pose animation one step, or copies the current pose into the working copy when no step is due.
pub fn func_800AF6E8(unit_opt: ?*v.D_800F4538_t) callconv(.c) void {
    const unit = unit_opt.?;
    c.func_800AFA28(@ptrCast(unit), @ptrCast(&unit.unkC54), 0);

    tail: {
        if (unit.unk5CC != 0) {
            if (unit.unk5CC == 1) {
                c.func_800AF844(@ptrCast(&unit.unk704.unk0), @ptrCast(&unit.unkC54.unk0), unit.unk0.nBones);
            }

            const v5b2: c_int = unit.unk5B2;

            if (v5b2 != 1) {
                const next: c_int = @as(c_int, unit.unk5CC) + 0xFF;
                unit.unk5CC = psx.conv(u8, v5b2 +% next);

                if (unit.unk5CC >= unit.unk5CD) {
                    unit.unk5CC = 0;
                    unit.unk5CD = 0;
                    break :tail;
                }
            }

            c.func_800B147C(
                @ptrCast(&unit.unk704),
                @ptrCast(&unit.unkC54),
                unit.unk0.nBones,
                unit.unk5CC,
                unit.unk5CD,
            );

            psx.setBits(&unit.unk704, "unk548_16", psx.getBits(&unit.unkC54, "unk548_16"));
            psx.setBits(&unit.unk704, "unk548_17", psx.getBits(&unit.unkC54, "unk548_17"));

            c.func_800B002C(@ptrCast(unit), 0);

            unit.unk5CC +%= 1;

            if (unit.unk5CC >= unit.unk5CD) {
                unit.unk5CC = 0;
                unit.unk5CD = 0;
            }
            return;
        }
    }

    c.vs_main_memcpy(@ptrCast(&unit.unk704), @ptrCast(&unit.unkC54), @as(c_uint, @sizeOf(v.D_800F4538_unkC54)));
    c.func_800B002C(@ptrCast(unit), 0);
}
