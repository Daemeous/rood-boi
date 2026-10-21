//! A point on a battle model's bone projected to the screen (src/BATTLE/BATTLE.PRG/38C1C.c).
const std = @import("std");
const common = @import("common");
const psx = common.psx;
const v = @import("views_38C1C");
const c = @import("c_38C1C");

/// orig: func_800A141C (src/BATTLE/BATTLE.PRG/38C1C.c)
/// Projects the bone's origin (mode 0), its end (1) or its middle (other) and stores the GTE's screen xy and depth words at `out`; -1 when the slot is empty.
pub fn projectBonePoint(slot: c_int, bone: c_int, out: c_int, mode: c_int) callconv(.c) c_int {
    var var_v1: ?*v.D_800F4538_t = psx.at(v.g.D_800F4538(), slot).*;
    if (var_v1 == null) {
        var_v1 = @ptrCast(psx.at(v.g.D_800F45E0(), slot).*);
        if (var_v1 == null) return -1;
    }
    const model = var_v1.?;

    const v0 = psx.at(&model.bones, bone);
    const temp = psx.get(model.unk0.unk68);

    c.SetRotMatrix(psx.cptr(c.MATRIX, v0));
    c.SetTransMatrix(psx.cptr(c.MATRIX, v0));

    const vx: i16 = switch (mode) {
        0 => 0,
        1 => psx.conv(i16, -%@as(c_int, psx.conv(u16, psx.at(&temp.?.armatures, bone).unk0))),
        else => psx.conv(i16, @divTrunc(-%psx.at(&temp.?.armatures, bone).unk0, 2)),
    };

    var sp0: v.SVECTOR = undefined;
    sp0.vx = vx;
    sp0.vy = 0;
    sp0.vz = 0;
    sp0.pad = 0;

    // gte_ldv0(&sp0): the two words of the vector, as the GTE reads them.
    const words: *const [8]u8 = @ptrCast(&sp0);
    c.MTC2(std.mem.readInt(u32, words[0..4], .little), 0);
    c.MTC2(std.mem.readInt(u32, words[4..8], .little), 1);

    // PC_GTE_UNNARROWED(gte_rtps()): the 4:3 projection, not widened.
    v.g.pc_gte_anchored().* +%= 1;
    _ = c.doCOP2(0x0180001);
    v.g.pc_gte_anchored().* -%= 1;

    const dst: *align(1) [2]c_uint = @ptrFromInt(@as(usize, psx.conv(u32, out)));
    dst[0] = c.MFC2(14);
    dst[1] = c.MFC2(19);

    return 0;
}
