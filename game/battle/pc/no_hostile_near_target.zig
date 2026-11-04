//! Whether no hostile actor stands near a target (D2: 73460.s; oracle pc/src/battle_movement.c).
const c = @import("c_battle_movement");
const v = @import("views_battle_movement");
const psx = @import("common").psx;

/// orig: guest_word (pc/src/battle_movement.c)
/// A word of PS1 main RAM through any of its KUSEG/KSEG0/KSEG1 mirrors (2 MB
/// repeated up to 8 MB), for the original's out-of-list reads. An unaligned
/// or non-RAM address would raise an address/bus error on the PS1.
fn guestWord(address: c_uint) c_uint {
    if ((address & 0x1fffffff) >= 0x00800000 or (address & 3) != 0)
        c.pc_unimplemented("actor engagement read outside guest RAM");
    return psx.byteAt(u32, @as([*]u8, @ptrFromInt(0x80000000)), @intCast(address & 0x1fffff)).*;
}

/// orig: func_800DBCEC (pc/src/battle_movement.c)
/// 1 unless some hostile actor (relation >= 3) stands within two tiles of
/// the engaged target of `id`. The original starts its list walk at
/// &vs_battle_actors itself, so the first "node" has id vs_battle_actors[1]
/// (a pointer, or 0); its relation word (0x1A4 + id * 4, wrapping) and, when
/// that reads as hostile, its model D_800F4538[id] (0x800F4538 + id * 4) and
/// the model's tile (+0x5C) are read through guest RAM as the PS1 does.
pub fn noHostileNearTarget(state: [*]u8, id: c_int) callconv(.c) c_int {
    var node: ?[*]u8 = @ptrCast(v.g.vs_battle_actors());
    while (node) |n| : (node = @ptrFromInt(psx.byteAt(u32, n, 0).*)) {
        const other: c_uint = psx.byteAt(u32, n, 4).*;
        const relation_address: c_uint = psx.addr(state) +% 0x1a4 +% (other << 2);
        const relation: c_uint = if (other < 16)
            psx.byteAt(u32, state, 0x1a4 +% @as(isize, @intCast(other * 4))).*
        else
            guestWord(relation_address);
        if (((relation >> 6) & 3) < 3) continue;
        const tile: c_uint = if (other < 16)
            psx.byteAt(u32, @as([*]u8, @ptrCast(v.g.D_800F4538()[other].?)), 0x5c).*
        else
            guestWord(guestWord(0x800f4538 +% (other << 2)) +% 0x5c);
        const target: [*]u8 = @ptrCast(psx.at(v.g.D_800F45E0(), id).*.?);
        if (c.func_800E45F4(tile, psx.byteAt(u32, target, 0x5c).*) < 3) return 0;
    }
    return 1;
}
