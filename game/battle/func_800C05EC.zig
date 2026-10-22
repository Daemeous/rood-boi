//! Stages a battle action request into the D_800EB9B8 block (src/BATTLE/BATTLE.PRG/573B8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_573B8");

/// orig: func_800C05EC (src/BATTLE/BATTLE.PRG/573B8.c)
/// Copies the request `arg0` into the block, then `arg1` (or clears it) with the flags and values given.
pub fn func_800C05EC(arg0: ?*v.D_800EB9B8_t2, arg1: ?*v.D_800EB9B8_t2, arg2: u8, arg3: c_int) callconv(.c) void {
    const block = v.g.D_800EB9B8().*.?;

    block.unk0 = arg0.?.*;

    if (arg0.?.unk0 != 1) {
        block.unk24 = 0;
        block.unk28 = 0;
    }

    if (v.g.D_800EB9AC().* != 0) {
        block.unk0.unk0 |= 0x80;
        block.unk10.unk0 = arg1.?.unk0;
        v.g.D_800EB9AC().* = 0;
        return;
    }

    block.unk38 = 0;
    block.unk3C = 0;
    block.unk2A = 0xF;

    if (arg1) |src| {
        block.unk10 = src.*;
        block.unk10.unk0 |= 0x80;
        block.unk2E = arg2;
        block.unk44 = arg3;
        return;
    }

    block.unk10.unk0 = 0;
}
