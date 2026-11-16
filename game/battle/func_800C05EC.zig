//! Stages a battle action request into the vs_battle_targetSphere block (pc/src/game/BATTLE/BATTLE.PRG/573B8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_573B8");

/// orig: func_800C05EC (pc/src/game/BATTLE/BATTLE.PRG/573B8.c)
/// Copies the request `arg0` into the block, then `arg1` (or clears it) with the flags and values given.
pub fn func_800C05EC(arg0: ?*v.func_800C1564_t, arg1: ?*v.func_800C1564_t, arg2: u8, arg3: ?*anyopaque) callconv(.c) void {
    const block = v.g.vs_battle_targetSphere().*.?;

    block.unk0[0] = arg0.?.*;

    if (arg0.?.unk0 != 1) {
        block.unk24 = 0;
        block.unk28 = 0;
    }

    if (v.g.D_800EB9AC().* != 0) {
        block.unk0[0].unk0 |= 0x80;
        block.unk0[1].unk0 = arg1.?.unk0;
        v.g.D_800EB9AC().* = 0;
        return;
    }

    block.unk38.words = .{ 0, 0 };
    block.unk2A = 0xF;

    if (arg1) |src| {
        block.unk0[1] = src.*;
        block.unk0[1].unk0 |= 0x80;
        block.unk2E = arg2;
        block.unk44 = @ptrCast(@alignCast(arg3));
        return;
    }

    block.unk0[1].unk0 = 0;
}
