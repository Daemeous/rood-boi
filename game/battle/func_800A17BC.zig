//! Matrix and armature lookup for an actor part (src/BATTLE/BATTLE.PRG/38C1C.c).
const psx = @import("common").psx;
const v = @import("views_38C1C");
const c = @import("c_38C1C");

/// orig: func_800A17BC (src/BATTLE/BATTLE.PRG/38C1C.c)
/// Copies an actor part's matrix to `outMatrix` and its armature's first value to `outArmature`.
pub fn func_800A17BC(actorSlot: c_int, armatureIndex: c_int, outMatrix: ?*anyopaque, outArmature: [*c]c_int) callconv(.c) c_int {
    // ROOD_PC: an empty actor slot gives NULL here; the PS1 copies the matrix from address 0 (0x80000000 on the host).
    const matrix = c.func_800A1DE8(actorSlot, armatureIndex, psx.cptr(c.MATRIX, v.g.D_800F49B8()));
    const src: *align(1) const v.MATRIX = if (matrix != null) @ptrCast(matrix) else psx.lowRam(v.MATRIX);
    const m = src.*;
    const dst: *align(1) v.MATRIX = @ptrCast(outMatrix.?);
    dst.* = m;

    const actor = psx.at(v.g.D_800F4538(), actorSlot).*;
    if (actor == null) {
        const objSlot = psx.at(v.g.D_800F45E0(), actorSlot).*;
        if (objSlot == null) return 0;
        const obj: *v.D_800F4538_t = @ptrCast(objSlot.?);
        outArmature.* = psx.at(&psx.get(obj.unk0.unk68).?.armatures, armatureIndex).unk0;
        return 0;
    }

    if (armatureIndex == 0xFF) {
        outArmature.* = 0;
    } else if ((armatureIndex & 0xF0) == 0x40) {
        const model = psx.at(v.g.vs_battle_wepModels(), actorSlot *% 2).*;
        if (model == null) return 0;
        const offsets: *v.D_800F4538_unk68 = @ptrCast(psx.get(model.?.offsets).?);
        outArmature.* = psx.at(&offsets.armatures, armatureIndex -% 0x3F).unk0;
    } else {
        outArmature.* = psx.at(&psx.get(actor.?.unk0.unk68).?.armatures, armatureIndex).unk0;
    }
    return 0;
}
