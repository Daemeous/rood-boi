//! zig_diff spec: func_800BDAB4 -> func_800BDAB4.func_800BDAB4 (4A0A8.c).
//! Inputs: a D_800F4BA4_t in the arena (its four func_800BDBB4_t blocks filled
//! with edge values: -1, 0, 255, 256, the i16 extremes). The four func_800BDBB4
//! tests take every combination of nonzero and zero, chosen by the class mask.
//! A nonzero return is 1, -1 or 0x10000, so both signs are tested. The stub
//! also rewrites the block it is passed (unk2/unk4), which the C reads back
//! for the copy after the call. The copy-out callees (func_8007DDB8,
//! func_8007DDD4, func_8007DDF8) receive a pointer to a stack temporary, so
//! their stubs copy what they are passed into an arena sink that the state
//! comparison covers: a wrong field copied into the temporary shows up there.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_4A0A8");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BDAB4));

pub const classes = blk: {
    var names: [16][:0]const u8 = undefined;
    for (0..16) |i| names[i] = std.fmt.comptimePrint("mask {d}", .{i});
    break :blk names;
};

/// Address of the D_800F4BA4 object this iteration; the stub finds the
/// block it was passed from its offset within that object.
var base: usize = 0;
/// Bit i set: the i-th func_800BDBB4 test (1FC, 21C, 23C, 25C) returns nonzero.
var mask: u4 = 0;
/// Arena bytes the copy-out stubs write what they are passed into.
var sink: [*]u8 = undefined;

const edges = [_]i16{ -1, 0, 255, 256, -256, 32767, -32768 };

fn fill(ctx: *zd.Ctx, o: *v.func_800BDBB4_t) void {
    o.unk2 = ctx.pick(i16, &edges);
    o.unk4 = ctx.pick(i16, &edges);
    o.unk6 = ctx.pick(i16, &edges);
}

fn sinkPut(off: usize, x: anytype) void {
    const bytes = std.mem.asBytes(&x);
    @memcpy(sink[off .. off + bytes.len], bytes);
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj = ctx.alloc(v.D_800F4BA4_t);
    v.g.D_800F4BA4().* = obj;
    base = @intFromPtr(obj);
    sink = ctx.allocBytes(64, 16);
    mask = ctx.int(u4);
    fill(ctx, &obj.unk1FC);
    fill(ctx, &obj.unk21C);
    fill(ctx, &obj.unk23C);
    fill(ctx, &obj.unk25C);
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    return mask;
}

pub const stubs = struct {
    pub fn func_800BDBB4(ctx: *zd.Ctx, p: anytype) c_int {
        const o: *v.func_800BDBB4_t = @ptrCast(@alignCast(p));
        if (ctx.value(u1) == 1) o.unk2 = ctx.value(i16);
        if (ctx.value(u1) == 1) o.unk4 = ctx.value(i16);
        const off = @intFromPtr(p) -% base;
        const idx: u2 = switch (off) {
            0x1FC => 0,
            0x21C => 1,
            0x23C => 2,
            0x25C => 3,
            else => 0,
        };
        if (((mask >> idx) & 1) == 0) return 0;
        const r = ctx.value(u8);
        return if (r < 128) 1 else if (r < 192) -1 else 0x10000;
    }

    pub fn func_8007DDB8(ctx: *zd.Ctx, p: anytype) void {
        _ = ctx;
        const d: *v.D_800F1A68_t = @ptrCast(@alignCast(p));
        sinkPut(0, d.unk0);
        sinkPut(4, d.unk4);
    }

    pub fn func_8007DDD4(ctx: *zd.Ctx, p: anytype) void {
        _ = ctx;
        const pc: *v.P_CODE = @ptrCast(@alignCast(p));
        sinkPut(8, pc.r0);
        sinkPut(9, pc.g0);
        sinkPut(10, pc.b0);
    }

    pub fn func_8007DDF8(ctx: *zd.Ctx, p: anytype) void {
        _ = ctx;
        const d: *v.D_800F1A68_t = @ptrCast(@alignCast(p));
        sinkPut(16, d.unk0);
        sinkPut(20, d.unk4);
    }
};
