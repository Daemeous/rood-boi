//! An effect-script opcode: spawns a child script (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// psx_set of a guest address: the 4-byte field's address on 64-bit, the pointer's bits on 32-bit (stored as
/// the C stores it: by its bits, so an unaligned address does not trap).
fn setAddress(field: anytype, address: u32) void {
    const F = @typeInfo(@TypeOf(field)).pointer.child;
    switch (@typeInfo(F)) {
        .optional, .pointer => @as(*usize, @ptrCast(field)).* = address,
        else => field.raw = address,
    }
}

/// orig: func_800D4778 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// func_800CE83C on a spawn record of the script's byte pointer, its next halfword and its unkD1C block; returns 1.
pub fn func_800D4778(script: ?*v.D_800F53B8_t) callconv(.c) c_int {
    const s = script.?;
    var spawn: v.D_800F53B8_t2 = undefined; // the C leaves the other fields uninitialised
    setAddress(&spawn.unk14, psx.rawOf(s.unkC));
    spawn.unk18 = c.func_800D5198(@ptrCast(s));
    psx.put(&spawn.unk0, &s.unkD1C);
    _ = c.func_800CE83C(@ptrCast(&spawn));
    return 1;
}
