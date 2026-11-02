//! A standing actor's step to the edge in front of it: the offset to the
//! platform's edge or to the next 128-unit cell, a probe move there, and
//! whether the move was blocked (src/BATTLE/BATTLE.PRG/40564.c).
const psx = @import("common").psx;
const v = @import("views_40564");
const c = @import("c_40564");

/// orig: func_800A4A24@40564.c (src/BATTLE/BATTLE.PRG/40564.c)
/// 40564's inlined copy of 3A1A0's func_800A4A24: lists the room's platforms
/// (func_8008D2C0) into its own static table (`platforms`, globalised by the
/// build: views func_800A4A24__platforms) and returns the entry whose id is
/// `arg0 - 2`; none: PS1 address 0, which the caller reads (the PS1 returned
/// NULL, and a pointer into its stack frame for a match: BUG in the original).
fn platformById(arg0: c_int) *v.func_8008D2C0_t {
    const table = v.g.func_800A4A24__platforms();
    const id = arg0 -% 2;
    const count = c.func_8008D2C0(@ptrCast(table));
    var i: c_int = 0;
    while (i < count) : (i += 1) {
        const entry = psx.at(table, i);
        if (@as(c_int, entry.id) == id) return entry;
    }
    return @alignCast(psx.lowRam(v.func_8008D2C0_t));
}

/// The actor's flag word at byte 0x5AC.
fn flagsOf(actor: *v.D_800F4538_t) *align(1) c_uint {
    return psx.byteAt(c_uint, actor, 0x5AC);
}

/// orig: func_800A9530 (src/BATTLE/BATTLE.PRG/40564.c)
/// Sets `offset` (x or z by the facing quadrant D_800F49F9 >> 1) to the step
/// from the actor's front (unk63C ahead) to the edge: its platform's edge
/// (unk0.unkC_0 nonzero) at 65 units, else the next 128-unit cell boundary
/// folded into -32..32 (plus a climb in vy). Probes the move with
/// func_800A70DC at rsin(0x200) of unk63C, the platform cleared, then undoes
/// it; 1 when the probe was not blocked (flag 0x1000 clear), else clears the
/// flag and returns 0.
pub fn func_800A9530(arg0: ?*v.D_800F4538_t, arg1: ?*v.SVECTOR) callconv(.c) c_int {
    const actor = arg0.?;
    const offset = arg1.?;

    var position: c_int = actor.unk63C;
    const direction: c_uint = @as(c_uint, v.g.D_800F49F9().*) >> 1;
    actor.unk1800 = psx.conv(i16, direction << 10);

    if (psx.getBits(&actor.unk0, "unkC_0") != 0) {
        const platform = platformById(psx.getBits(&actor.unk0, "unkC_0"));
        var edge: c_int = 65;
        if (@as(c_int, @bitCast(direction)) < 2) {
            position = -%position;
        } else {
            edge = -65;
        }
        if (direction & 1 != 0) {
            position +%= actor.unk0.position.vx;
            edge +%= platform.x;
            offset.vx = psx.conv(i16, edge -% position);
        } else {
            position +%= actor.unk0.position.vz;
            edge +%= platform.z;
            offset.vz = psx.conv(i16, edge -% position);
        }
    } else {
        var edge: c_int = 0;
        if (@as(c_int, @bitCast(direction)) < 2) {
            position = -%position;
        } else {
            edge = 127;
        }
        if (direction & 1 != 0) {
            position +%= actor.unk0.position.vx;
            offset.vx = psx.conv(i16, edge -% (position & 127));
        } else {
            position +%= actor.unk0.position.vz;
            offset.vz = psx.conv(i16, edge -% (position & 127));
        }
        if (offset.vx > 32) offset.vx = psx.conv(i16, @as(c_int, offset.vx) - 64);
        if (offset.vx < -32) offset.vx = psx.conv(i16, @as(c_int, offset.vx) + 64);
        if (offset.vz > 32) offset.vz = psx.conv(i16, @as(c_int, offset.vz) - 64);
        if (offset.vz < -32) offset.vz = psx.conv(i16, @as(c_int, offset.vz) + 64);
        if (flagsOf(actor).* & 0xF000_0000 != 0) {
            offset.vy = psx.conv(i16, @as(c_int, offset.vy) + 12);
        }
        offset.vy = psx.conv(i16, @as(c_int, offset.vy) + 8);
    }

    const surface = psx.getBits(&actor.unk0, "unkC_0");
    psx.setBits(&actor.unk0, "unkC_0", 0);
    actor.unk0.position.vx = psx.conv(i16, @as(c_int, actor.unk0.position.vx) + offset.vx);
    actor.unk0.position.vz = psx.conv(i16, @as(c_int, actor.unk0.position.vz) + offset.vz);
    var distance = c.rsin(0x200);
    distance *%= @as(c_int, actor.unk63C);
    distance = @divTrunc(distance, 4096);
    c.func_800A70DC(@ptrCast(actor), distance);
    actor.unk0.position.vx = psx.conv(i16, @as(c_int, actor.unk0.position.vx) - offset.vx);
    actor.unk0.position.vz = psx.conv(i16, @as(c_int, actor.unk0.position.vz) - offset.vz);
    psx.setBits(&actor.unk0, "unkC_0", surface);

    const flags = flagsOf(actor).*;
    if (flags & 0x1000 == 0) return 1;
    flagsOf(actor).* = flags & ~@as(c_uint, 0x1000);
    return 0;
}
