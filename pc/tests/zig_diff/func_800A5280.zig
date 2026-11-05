//! zig_diff spec: func_800A5280 -> func_800A5280.func_800A5280 (3A1A0.c).
//! Inputs: an actor in the arena installed at table slot `index` (0 half the
//! time; sometimes index >= 17 or an empty slot, and sometimes a guard of the
//! prologue tripped: unk5AC_0, status bits 24-27, flags bit 20, a positive
//! unk11). The status word (u32 at byte 8) is drawn from the normal, falling
//! (bit 20), turning (bit 19) and 0x70000 classes; the flags word (byte 0x5AC)
//! gets bits 3, 12 and 15 and the object nibble at random; unk5B9, unk5BA,
//! unk5BE, the coast counters (unk1840..unk1847), unk6E4, unk17FF, the animation
//! id, the tile bytes and the 0x18D2..0x18D8 history are drawn from edge sets.
//! `distance` is -1 (stick), 0, and values around unk5B9 << 12 and the
//! wrap-around extremes; `direction` is relative to unk1800 so the +-0x400
//! turn tests see their edges. The stubs stand in for func_800A76BC (status
//! 0..3 and others, the out-param result -3..7, motion changes, the history
//! words made equal sometimes) and the terrain height callee func_800A6EE8
//! (offsets around the tested thresholds); they write what the real callees
//! can write (area, probe globals) and record the motion vectors they are
//! handed (only the parts the C has defined at that point).
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_3A1A0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A5280));

pub const classes = [_][:0]const u8{
    "guard-return",
    "fall-state",
    "turn-state",
    "state-0x70000",
    "stick-input",
    "idle-distance",
    "walk",
};

pub const ignore_return = "the C returns `status` before func_800A76BC has set it on its two `goto done` paths of the idle and scale code (a register's leftover); its one caller, func_800A4E68, ignores the result";

/// Whether the C has set `facing` by the time it passes it to func_800A6660 at the end (the C's walk
/// code sets it; the stick and idle paths do not): decided by prepare.
var facing_defined: bool = true;

pub fn uninitialisedArgument(call: []const u8, index: usize) bool {
    return !facing_defined and index == 1 and std.mem.eql(u8, call, "func_800A6660");
}

fn status(a: *v.D_800F4538_t) *align(1) u32 {
    return psx.byteAt(u32, a, 8);
}

fn flags(a: *v.D_800F4538_t) *align(1) u32 {
    return psx.byteAt(u32, a, 0x5AC);
}

fn actorOf(index: anytype) ?*v.D_800F4538_t {
    const i: usize = @intCast(index);
    if (i >= 17) return null;
    return v.g.D_800F4538()[i];
}

/// A small or full-range i16 from the stub's deterministic stream.
fn small(ctx: *zd.Ctx) i16 {
    return switch (ctx.value(u3)) {
        0 => 0,
        1 => ctx.value(i16),
        2 => 4096,
        3 => -4096,
        else => @as(i16, ctx.value(i6)),
    };
}

/// What a callee that was handed a motion vector sees of it: x and z always, y where the C has set it.
fn record(ctx: *zd.Ctx, p: anytype, with_y: bool) void {
    const sv: *const v.SVECTOR = @ptrCast(@alignCast(p));
    const seen = ctx.alloc(v.SVECTOR);
    seen.vx = sv.vx;
    seen.vz = sv.vz;
    if (with_y) seen.vy = sv.vy;
}

/// The C's `reject` stores `previousArea`, which only the statuses without bits 16-20 define: otherwise it is
/// stack garbage the harness cannot reproduce (0x18(%rsp) of the object). Those states never reach `reject`
/// here: the 0x70000-with-0x1000 shortcut is excluded in prepare and the callees below that can send the
/// step to `reject` always accept it.
fn areaUndefined(p: anytype) bool {
    const act: *v.D_800F4538_t = @ptrCast(@alignCast(p));
    return (status(act).* & 0x1F0000) != 0;
}

pub const stubs = struct {
    pub fn func_800A0204(ctx: *zd.Ctx, a: anytype, b: anytype, c: anytype, d: anytype) c_int {
        _ = a;
        _ = b;
        _ = c;
        _ = d;
        return ctx.value(c_int);
    }

    pub fn func_800ACF54(ctx: *zd.Ctx, p: anytype) void {
        const act: *v.D_800F4538_t = @ptrCast(@alignCast(p));
        if (ctx.value(u2) == 0) psx.setBits(&act.unk0, "unkC_0", ctx.value(u4));
    }

    pub fn func_800A6EE8(ctx: *zd.Ctx, p: anytype, x: anytype, z: anytype, mode: anytype) c_int {
        _ = x;
        _ = z;
        _ = mode;
        const pos: *const v.SVECTOR = @ptrCast(@alignCast(p));
        if (ctx.value(u3) == 0) return ctx.value(c_int);
        if (ctx.value(u2) == 0) return @as(c_int, pos.vy);
        const deltas = [_]c_int{ -0x21, -0x20, -0x1F, -0xEC, -0xEB, -0xED, -0x60, -0x5F, -0x5E, -3, -2, -1, 0, 0, 0, 0, 1, 2, 0x3F, 0x40, 0x41, 0xFF, 0x100, 0x101, -0x100, -0xFF };
        return @as(c_int, pos.vy) +% deltas[@as(usize, ctx.value(u8)) % deltas.len];
    }

    pub fn func_800A6660(ctx: *zd.Ctx, a: anytype, facing: anytype, input: anytype) void {
        _ = ctx;
        _ = a;
        _ = facing;
        _ = input;
    }

    pub fn func_800A6798(ctx: *zd.Ctx, a: anytype, offset: anytype, saved: anytype) c_int {
        record(ctx, offset, true);
        record(ctx, saved, true);
        const r = @as(c_int, ctx.value(u2));
        return if (areaUndefined(a) and r == 0) 1 else r;
    }

    pub fn func_800A69B4(ctx: *zd.Ctx, a: anytype) c_int {
        const r = @as(c_int, ctx.value(u2));
        return if (areaUndefined(a) and r == 0) 1 else r;
    }

    pub fn func_800A6AA0(ctx: *zd.Ctx, a: anytype, distance: anytype, direction: anytype) c_int {
        _ = a;
        _ = distance;
        _ = direction;
        return ctx.value(c_int);
    }

    pub fn func_800A76BC(ctx: *zd.Ctx, p: anytype, m: anytype, r: anytype, probe: anytype) c_int {
        _ = probe;
        record(ctx, m, false);
        const act: *v.D_800F4538_t = @ptrCast(@alignCast(p));
        const mv: *v.SVECTOR = @ptrCast(@alignCast(m));
        const st: c_int = switch (ctx.value(u3)) {
            0, 7 => 0,
            1 => 1,
            2 => 2,
            3, 4 => 3,
            5 => 0,
            else => @as(c_int, ctx.value(u8)),
        };
        if (ctx.value(u2) == 0) mv.vx = small(ctx);
        if (ctx.value(u2) == 0) mv.vz = small(ctx);
        r[0] = switch (ctx.value(u4)) {
            0 => -1,
            1, 2 => 0,
            3, 4 => 1,
            5, 6 => 2,
            7, 8 => 3,
            9, 10 => 4,
            11 => 5,
            12 => 6,
            13 => 7,
            14 => -3,
            else => @as(c_int, ctx.value(i8)),
        };
        if (ctx.value(u1) == 0) {
            const probe_pos = v.g.D_800F4B08();
            probe_pos.vx = ctx.value(i16);
            probe_pos.vy = small(ctx);
            probe_pos.vz = ctx.value(i16);
            probe_pos.pad = ctx.value(i16);
        }
        // sometimes the actor's remembered area is the probe's: the C's `unk61 != unkC_0` test
        if (ctx.value(u1) == 0) act.unk0.unk61 = @truncate(@as(u16, @bitCast(v.g.D_800F4B08().pad)) & 0xF);
        if (ctx.value(u2) == 0) v.g.D_800F49F4().* = ctx.value(u16);
        if (ctx.value(u2) == 0) v.g.D_800F49F8().* = ctx.value(u8);
        if (ctx.value(u2) == 0) v.g.D_800F49F9().* = ctx.value(u8);
        // The hold test of the 0x1000 flag: the history words equal to position + motion.
        if (ctx.value(u2) == 0) {
            const mx: c_int = if (st == 3) 0 else @as(c_int, mv.vx);
            const mz: c_int = if (st == 3) 0 else @as(c_int, mv.vz);
            psx.byteAt(u16, act, 0x18D6).* = psx.conv(u16, @as(c_int, act.unk0.position.vx) +% mx);
            if (ctx.value(u2) != 0) psx.byteAt(u16, act, 0x18D8).* = psx.conv(u16, @as(c_int, act.unk0.position.vz) +% mz);
        }
        return st;
    }

    pub fn func_800A9530(ctx: *zd.Ctx, a: anytype, m: anytype) c_int {
        const mv: *v.SVECTOR = @ptrCast(@alignCast(m));
        if (ctx.value(u2) == 0) mv.vy = small(ctx);
        record(ctx, m, true);
        const r = @as(c_int, ctx.value(u2));
        return if (areaUndefined(a) and r == 0) 1 else r;
    }

    pub fn func_800AA600(ctx: *zd.Ctx, index: anytype, m: anytype, n: anytype) void {
        _ = index;
        _ = n;
        record(ctx, m, true);
    }

    pub fn func_800AA698(ctx: *zd.Ctx, index: anytype, m: anytype, n: anytype) void {
        _ = index;
        _ = n;
        record(ctx, m, true);
    }

    pub fn func_800AA850(ctx: *zd.Ctx, index: anytype, direction: anytype, n: anytype) void {
        _ = ctx;
        _ = index;
        _ = direction;
        _ = n;
    }

    pub fn func_800AE4FC(ctx: *zd.Ctx, p: anytype, n: anytype) void {
        _ = ctx;
        _ = p;
        _ = n;
    }

    pub fn func_800AE6C0(ctx: *zd.Ctx, a: anytype, b: anytype, c: anytype) void {
        _ = ctx;
        _ = a;
        _ = b;
        _ = c;
    }

    pub fn func_800A9378(ctx: *zd.Ctx, x: anytype, y: anytype, z: anytype, n: anytype) c_int {
        _ = x;
        _ = y;
        _ = z;
        _ = n;
        if (ctx.value(u1) == 0) v.g.D_800F49F4().* = ctx.value(u16);
        return ctx.value(c_int);
    }

    pub fn func_800A3054(ctx: *zd.Ctx, a: anytype, m: anytype) void {
        _ = a;
        record(ctx, m, true);
    }

    pub fn rcos(ctx: *zd.Ctx, angle: anytype) c_int {
        _ = angle;
        return switch (ctx.value(u3)) {
            0 => 4096,
            1 => -4096,
            2 => 0,
            else => @as(c_int, ctx.value(i13)),
        };
    }

    pub fn rsin(ctx: *zd.Ctx, angle: anytype) c_int {
        _ = angle;
        return switch (ctx.value(u3)) {
            0 => 4096,
            1 => -4096,
            2 => 0,
            else => @as(c_int, ctx.value(i13)),
        };
    }
};

fn smallPosition(ctx: *zd.Ctx) i16 {
    return ctx.pick(i16, &.{ 0, -1, 1, 127, -127, 128, -128, 2047, -2048, 32767, -32768 });
}

pub fn prepare(ctx: *zd.Ctx) Args {
    // Globals the C reads.
    const table = v.g.D_800F4538();
    for (table) |*e| e.* = null;
    const object = ctx.alloc(v.D_800F45E0_t);
    object.unk1A = ctx.pick(i16, &.{ 0xFD, 0xFD, 0xFD, 0xFC, 0xFE, -3, 0 });
    object.unk6C[8].unk4 = ctx.pick(u8, &.{ 0, 1, 7, 13, 14, 15, 16, 255 });
    for (v.g.D_800F45E0()) |*e| e.* = object;
    const bits = ctx.alloc(u32);
    bits.* = ctx.int(u32);
    bits.* = (bits.* & ~@as(u32, 0x20)) | (if (ctx.chance(2)) @as(u32, 0x20) else 0);
    v.g.D_800F49F0().* = @ptrCast(bits);
    for (0..16) |k| v.g.D_800F1D6E()[k] = if (ctx.chance(3)) 7 else ctx.int(u8);
    v.g.D_800F49F4().* = ctx.int(u16);
    v.g.D_800F49F8().* = ctx.int(u8);
    v.g.D_800F49F9().* = ctx.int(u8);
    v.g.vs_gametime_tickspeed().* = ctx.pick(c_int, &.{ 0, 1, 2, 3, 4, 5, 6, 8, 16, -2, 0x7FFFFFFF });
    v.g.D_800E90BC().* = ctx.int(i32);
    const probe = v.g.D_800F4B08();
    probe.vx = smallPosition(ctx);
    probe.vy = smallPosition(ctx);
    probe.vz = smallPosition(ctx);
    probe.pad = ctx.pick(i16, &.{ 0, 0x100, 0x1FF, -1, 0x7FFF, -32768, 0xF00 });

    // The actor and its slot.
    var index: c_int = if (ctx.chance(2)) 0 else ctx.range(c_int, 1, 16);
    const bad_index = ctx.chance(24);
    const empty_slot = !bad_index and ctx.chance(24);
    if (bad_index) index = ctx.edge(c_int, &.{ 17, 18, 255, 0x7FFFFFFF });
    const a = ctx.alloc(v.D_800F4538_t);
    if (!bad_index and !empty_slot) table[@intCast(index)] = a;

    // The prologue's guards: each tripped one run in six.
    const guard = ctx.chance(6);
    var st: u32 = ctx.int(u32) & ~@as(u32, 0x0F000000);
    var fl: u32 = ctx.int(u32) & ~@as(u32, 0x100000);
    fl = (fl & ~@as(u32, 0x9008)) | (if (ctx.chance(3)) @as(u32, 0x1000) else 0) | (if (ctx.chance(3)) @as(u32, 0x8000) else 0) | (if (ctx.chance(4)) @as(u32, 0x8) else 0);
    if (ctx.chance(3)) fl &= 0x0FFFFFFF;
    if (guard and ctx.chance(3)) st |= @as(u32, ctx.range(u4, 1, 15)) << 24;
    if (guard and ctx.chance(3)) fl |= 0x100000;
    a.unk0.unk11 = if (guard and ctx.chance(3)) ctx.range(u8, 1, 127) else ctx.edge(u8, &.{ 0, 0x80, 0xFE, 0xFF });
    // Class of the status word: bits 16-18 (0x70000), bit 19 (turning), bit 20 (falling).
    st &= ~@as(u32, 0x1F0000);
    switch (ctx.range(u8, 0, 9)) {
        0, 1, 2, 3, 4 => {},
        5 => st |= 0x100000 | (if (ctx.chance(4)) @as(u32, ctx.range(u3, 0, 7)) << 16 else 0),
        6 => st |= ctx.edge(u32, &.{ 0x80000, 0x180000 }) | (if (ctx.chance(4)) @as(u32, ctx.range(u3, 0, 7)) << 16 else 0),
        else => st |= @as(u32, ctx.edge(u3, &.{ 1, 2, 3, 3, 4, 5, 6, 7 })) << 16,
    }
    // `reject` would store the C's uninitialised previousArea (see areaUndefined). In a 0x70000 state without
    // bits 19-20 it is reached through the 0x1000 shortcut, bit 23, unk5AC_3 and the pad == 0 checks: none of
    // them is set there.
    const walking = (st & 0x70000) != 0 and (st & 0x180000) == 0;
    if (walking) {
        fl &= ~@as(u32, 0x1000 | 0x8);
        st &= ~@as(u32, 0x800000);
    }
    status(a).* = st;
    flags(a).* = fl;
    psx.setBits(a, "unk5AC_0", if (guard and ctx.chance(3)) ctx.range(u2, 1, 3) else 0);

    a.unk5B9 = ctx.pick(u8, &.{ 0, 1, 2, 3, 4, 5, 7, 8, 255 });
    a.unk5BA = ctx.pick(u8, &.{ 0, 1, 2, 3, 4, 5, 8, 255 });
    a.unk5BE = ctx.pick(i16, &.{ 4, 4, 4, 0, 1, 2, 3, 5, 6, -1, 8, -4, 0x100, 0x7FFF, -32768 });
    const b9: i32 = a.unk5B9;
    a.unk1840 = ctx.pick(i16, &.{ 0, 1, -1, @as(i16, @intCast(b9)), @as(i16, @intCast(b9 + 1)), @as(i16, @intCast(b9 - 1)), 0x7FFF, -32768, 20 });
    a.unk1842 = ctx.int(i16);
    a.unk1846 = ctx.pick(u8, &.{ 0, 19, 20, 21, 255 });
    a.unk1847 = ctx.pick(u8, &.{ 0, 8, 9, 10, 255 });
    a.unk6E4 = ctx.pick(u8, &.{ 0, 1, 2, 255 });
    a.unk17FF = ctx.pick(u8, &.{ 0, 7, 8, 9, 255 });
    a.animationId = ctx.pick(u8, &.{ 0, 1, 2, 13, 14, 15, 26, 27, 0x13, 0x14, 0x54, 99, 100, 101, 112, 113, 114, 127, 128, 0xFF });
    a.unk6EE = ctx.pick(u8, &.{ 0, 1, 0x7F, 0x80, 0xFF });
    a.unk6EF = ctx.pick(u8, &.{ 0, 1, 0x7F, 0x80, 0xFF });
    a.unk6EC = ctx.int(i16);
    a.unk1800 = ctx.int(i16);
    a.unk0.unk1A = ctx.pick(u16, &.{ 0, 0, 1, 0xFFFF });
    a.unk0.facing = ctx.int(i16);
    a.unk0.unk3E = ctx.int(i16);
    a.unk0.position.vx = smallPosition(ctx);
    a.unk0.position.vy = smallPosition(ctx);
    a.unk0.position.vz = smallPosition(ctx);
    psx.setBits(&a.unk0, "unkC_0", if (ctx.chance(3)) 0 else ctx.range(u4, 0, 15));
    psx.setBits(&a.unk0, "unkC_4", ctx.range(u4, 0, 15));
    // Tile bytes 0x5C..0x62: the masked words equal half the time, unk61 equal to unkC_0 half the time.
    if (ctx.chance(2)) {
        // the C sets currentTile from the position / 128 before comparing: make the last tile equal that
        const T = @TypeOf(a.unk0.currentTileX);
        a.unk0.lastTouchedTileX = psx.conv(T, @divTrunc(@as(i32, a.unk0.position.vx), 128));
        a.unk0.lastTouchedTileZ = psx.conv(T, @divTrunc(@as(i32, a.unk0.position.vz), 128));
    }
    if (ctx.chance(2)) a.unk0.unk61 = psx.getBits(&a.unk0, "unkC_0");
    for ([_]usize{ 0x18D2, 0x18D4, 0x18D6, 0x18D8 }) |off| psx.byteAt(u16, a, @intCast(off)).* = ctx.pick(u16, &.{ 0, 1, 0xFFFF, 0x8000 });
    v.g.D_800F4B0A().* = a.unk0.position.vy +% ctx.pick(i16, &.{ 0, 1, -1, -0x5F, -0x60, -0x5E, -0xEC, -0xEC, -0xEC, -0xEB, -0xED, 0x100 });

    // The arguments.
    const base: c_int = @as(c_int, a.unk5B9) << 12;
    const distance: c_int = switch (ctx.range(u8, 0, 12)) {
        0, 1, 2 => -1,
        3, 4 => 0,
        5 => base -% 1,
        6 => base,
        7 => base +% 1,
        8 => ctx.range(c_int, 1, 0xFFF),
        9 => ctx.range(c_int, 0x1000, 0x20000),
        10 => ctx.edge(c_int, &.{ -2, -0x1000, -0x7FFFFFFF - 1, 0x7FFFFFFF, 0x1F000, 0x1EFFF, 0x1F001, 0x3DFFE, 0x7BFFC }),
        11 => @as(c_int, a.unk5BA) << 12,
        else => ctx.int(c_int),
    };
    const turn = @as(c_int, psx.conv(u16, a.unk1800));
    var direction: c_int = if (ctx.chance(2))
        turn +% ctx.pick(c_int, &.{ 0, 1, 0x3FF, 0x400, 0x401, 0x800, 0xBFF, 0xC00, 0xC01, 0xFFF, 0x1000, -1, -0x400, -0x1000 })
    else
        ctx.pick(c_int, &.{ 0, 0x400, 0x800, 0xC00, 0xFFF, 0x1000, -1, 0x7FFFFFFF });

    const input = ctx.alloc(v.SVECTOR);
    input.vx = ctx.pick(i16, &.{ 0, 0, 1, -1, 0x7FFF, -0x8000, 100 });
    input.vz = ctx.pick(i16, &.{ 0, 0, 1, -1, 0x7FFF, -0x8000, 100 });
    input.pad = if (walking) ctx.pick(i16, &.{ 1, 1, 2, -1, 0x100 }) else ctx.pick(i16, &.{ 0, 0, 1, 1, 2, -1, 0x100 });

    // A falling actor (no 0x70000 bits) stepping straight on with the stick off: the `unk17FF` shortcut's return.
    if ((st & 0x1F0000) == 0x100000 and ctx.chance(2)) {
        direction = turn +% ctx.pick(c_int, &.{ 0, 1, 0x100, -0x100 });
        a.unk0.unk1A = 0;
        input.pad = 0;
        a.unk17FF = ctx.pick(u8, &.{ 0, 1, 7 });
    }
    // The scale paths' boundaries: speed == 0x1EFFF (distance * unk5BE / 4), the coast counter reaching zero.
    if (distance == 0x3DFFE) a.unk5BE = 2;
    if (distance == 0x7BFFC) a.unk5BE = 1;
    const tick = v.g.vs_gametime_tickspeed().*;
    if (tick >= 0 and tick <= 0x7FFF and ctx.chance(4)) a.unk1840 = @intCast(tick);
    // The idle path (distance 0) down to its animation tests: no coast, no counters, nothing held.
    if (distance == 0 and ctx.chance(2)) {
        flags(a).* &= ~@as(u32, 0x1000 | 0x8000);
        a.unk6E4 = 0;
        a.unk1840 = 0;
        psx.setBits(&a.unk0, "unkC_0", 0);
    }

    // Whether the C has set `facing` when it passes it on (the idle and stick paths leave it).
    var d = distance;
    if ((st & 0x180000) == 0x100000 and d >= base) d = @as(c_int, a.unk5BA) << 12;
    if ((st & 0x70000) != 0 and @divTrunc(d, 4096) >= @as(c_int, a.unk5B9) and (psx.byteAt(u32, a, 0x5B0).* & 4) == 0) d = @as(c_int, a.unk5BA) << 12;
    facing_defined = d != -1 and d != 0;

    // The C reads `input` only past the prologue: null there only when the prologue returns.
    const returns_early = bad_index or empty_slot or psx.getBits(a, "unk5AC_0") != 0 or (st & 0x0F000000) != 0 or (fl & 0x100000) != 0 or @as(i8, @bitCast(a.unk0.unk11)) > 0;
    const pointer: ?*v.SVECTOR = if (returns_early and ctx.chance(2)) null else input;
    return .{ index, direction, distance, pointer };
}

pub fn classify(args: Args) usize {
    const a = actorOf(args[0]) orelse return 0;
    const st = status(a).*;
    const fl = flags(a).*;
    if (psx.getBits(a, "unk5AC_0") != 0 or (st & 0x0F000000) != 0 or (fl & 0x100000) != 0 or @as(i8, @bitCast(a.unk0.unk11)) > 0) return 0;
    if ((st & 0x180000) == 0x100000) return 1;
    if ((st & 0x180000) != 0) return 2;
    if ((st & 0x70000) != 0) return 3;
    if (args[2] == -1) return 4;
    if (args[2] == 0) return 5;
    return 6;
}
