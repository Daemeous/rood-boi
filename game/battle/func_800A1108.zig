//! Battle actor lookup: copies a slot's tile, position and facing out (src/BATTLE/BATTLE.PRG/30DB0.c).
const psx = @import("common").psx;
const v = @import("views_30DB0");

/// orig: func_800A1108 (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Copies the tile, position and facing of actor slot `arg0` into `arg1`; -1 if the slot is empty.
pub fn func_800A1108(arg0: c_int, arg1: ?*anyopaque) callconv(.c) c_int {
    var found: ?*v.D_800F4538_t = psx.at(v.g.D_800F4538(), arg0).*;
    if (found == null) {
        found = @ptrCast(psx.at(v.g.D_800F45E0(), arg0).*);
        if (found == null) return -1;
    }
    const t = found.?;

    // *(int*)&field reads four bytes at the field's address, unaligned on the guest side.
    const tile: *align(1) c_int = @ptrCast(&t.unk0.currentTileX);
    const pos_xy: *align(1) c_int = @ptrCast(&t.unk0.position.vx);
    const pos_z: *align(1) c_int = @ptrCast(&t.unk0.position.vz);

    const out: [*]align(1) c_int = @ptrCast(arg1.?);
    out[0] = tile.*;
    out[1] = pos_xy.*;
    out[2] = pos_z.*;

    // Written after the ints: its bytes overlap out[2]'s upper half, as in the C.
    const out16: [*]align(1) u16 = @ptrCast(arg1.?);
    out16[5] = psx.conv(u16, t.unk0.facing);
    return 0;
}
