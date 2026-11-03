//! zig_diff spec: func_800BD6C4 -> func_800BD6C4.func_800BD6C4 (4A0A8.c).
//! Inputs: a script of 8 random bytes whose first byte is one of the eight
//! opcodes the C handles (185, 186, 228, 229, 230, 231, 237, 191), or, one
//! time in eight, one outside the jump table's range 185..237 that it does
//! not handle (the original falls off the end: its object returns the opcode
//! + 0x47 there, both targets; inside the range it returns a code address, so
//! those opcodes are not fed), with
//! script[4] and script[5] zero a quarter of the time and edge bytes
//! (0x80, 0xff) otherwise, so the immediate and interpolated paths both run
//! and the signed-char casts see negative bytes. D_800F4BA4 points at an
//! arena block; its unk25C.unk1E is 0, 1, -1, 32767 or -32768, so the busy
//! test `> 0` sees equal sides often. The mode that vs_battle_getShort returns
//! is zero a quarter of the time and otherwise a signed edge. The out-parameter
//! callees write the fields the C reads back. The stack structs passed to the
//! immediate setters are not part of the compared state, so each setter logs
//! the fields it receives into an arena block (`sink`), which is compared.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BD6C4));

pub const classes = [_][:0]const u8{
    "185",
    "186",
    "228 immediate",
    "228 interpolated",
    "229 immediate",
    "229 interpolated",
    "230 immediate",
    "230 interpolated",
    "231",
    "237 busy",
    "237 warp",
    "237 interpolated",
    "191 mode zero",
    "191 mode nonzero",
    "no case",
};

/// The value vs_battle_getShort returns this iteration (set by prepare).
var mode_value: c_short = 0;
/// Arena block the logging stubs write into (set by prepare; compared as state).
var sink: *[16]i32 = undefined;

pub const stubs = struct {
    pub fn vs_battle_getShort(ctx: *zd.Ctx, p: anytype) c_short {
        _ = ctx;
        _ = p;
        return mode_value;
    }

    // Immediate setters: log the fields they receive.
    pub fn func_8007DDB8(ctx: *zd.Ctx, p: anytype) void {
        _ = ctx;
        const a: *const v.D_800F1A68_t = @ptrCast(@alignCast(p));
        sink[0] = a.unk0;
        sink[1] = a.unk4;
    }

    pub fn func_8007DDF8(ctx: *zd.Ctx, p: anytype) void {
        _ = ctx;
        const a: *const v.D_800F1A68_t = @ptrCast(@alignCast(p));
        sink[2] = a.unk0;
        sink[3] = a.unk4;
    }

    pub fn func_8007DDD4(ctx: *zd.Ctx, p: anytype) void {
        _ = ctx;
        const a: *const v.P_CODE = @ptrCast(@alignCast(p));
        sink[4] = a.r0;
        sink[5] = a.g0;
        sink[6] = a.b0;
    }

    pub fn func_8007DE44(ctx: *zd.Ctx, x: anytype) void {
        _ = ctx;
        sink[7] = @bitCast(@as(u32, @intCast(x)));
    }

    pub fn func_8007DE2C(ctx: *zd.Ctx, x: anytype) void {
        _ = ctx;
        sink[8] = @intCast(x);
    }

    pub fn func_8007DE5C(ctx: *zd.Ctx, x: anytype) void {
        _ = ctx;
        sink[9] = @intCast(x);
    }

    pub fn func_8007DD50(ctx: *zd.Ctx, x: anytype) void {
        _ = ctx;
        sink[10] = @intCast(x);
    }

    // Out-parameter callees: each writes the fields the C reads back.
    pub fn func_8007DE88(ctx: *zd.Ctx, p: anytype) void {
        const a: [*]c_int = @ptrCast(@alignCast(p));
        a[0] = ctx.value(c_int);
        a[1] = ctx.value(c_int);
    }

    pub fn func_8007DECC(ctx: *zd.Ctx, p: anytype) void {
        const a: [*]c_int = @ptrCast(@alignCast(p));
        a[0] = ctx.value(c_int);
        a[1] = ctx.value(c_int);
    }

    pub fn func_8007DEA8(ctx: *zd.Ctx, p: anytype) void {
        const a: *v.P_CODE = @ptrCast(@alignCast(p));
        a.r0 = ctx.value(u8);
        a.g0 = ctx.value(u8);
        a.b0 = ctx.value(u8);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    sink = ctx.alloc([16]i32);

    const s: [*c]u8 = @ptrCast(ctx.allocBytes(8, 1));
    for (1..8) |i| s[i] = ctx.int(u8);
    const ops = [_]u8{ 185, 186, 228, 229, 230, 231, 237, 191 };
    s[0] = ctx.edge(u8, &ops);
    if (ctx.chance(8)) s[0] = ctx.edge(u8, &.{ 0, 1, 100, 184, 238, 239, 255 });
    s[4] = if (ctx.chance(3)) 0 else ctx.pick(u8, &.{ 1, 0x7f, 0x80, 0xff });
    s[5] = if (ctx.chance(3)) 0 else ctx.pick(u8, &.{ 1, 0x7f, 0x80, 0xff });

    const b = ctx.alloc(v.D_800F4BA4_t);
    v.g.D_800F4BA4().* = b;
    b.unk25C.unk1E = ctx.edge(i16, &.{ 0, 0, 1, -1, 32767, -32768 });

    mode_value = if (ctx.chance(4)) 0 else ctx.pick(c_short, &.{ 1, -1, 32767, -32768 });
    return .{ s, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    const s = args[0];
    const b = v.g.D_800F4BA4().*.?;
    return switch (s[0]) {
        185 => 0,
        186 => 1,
        228 => if (s[4] == 0) 2 else 3,
        229 => if (s[5] == 0) 4 else 5,
        230 => if (s[4] == 0) 6 else 7,
        231 => 8,
        237 => if (b.unk25C.unk1E > 0) 9 else if (s[4] == 0) 10 else 11,
        191 => if (mode_value == 0) 12 else 13,
        else => 14,
    };
}
