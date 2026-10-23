//! zig_diff spec: func_800A51A0 -> func_800A51A0.func_800A51A0 (3A1A0.c).
//! Inputs: an SVECTOR in the arena and a table of one to four func_8008D2C0_t
//! entries. The func_8008D2C0 stub copies the table into the C's sp10 and
//! returns its count. arg0 always matches an entry's unk6 (after the C's
//! `arg0 -= 2`): the C dereferences p with no null test, so a run where no
//! entry matches would fault in the C itself. The position sits exactly on
//! each box bound (the ordered comparisons see equal), just outside it, at
//! the far side, or at a random i16, and the entry values take the i16
//! edges. The C computes in int, so nothing wraps. arg1 is never null.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_3A1A0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A51A0));

pub const classes = [_][:0]const u8{ "x-high-out", "x-low-out", "z-high-out", "z-low-out", "inside" };

/// The table the stub copies into sp10 and the count it returns.
var tbl: [4]v.func_8008D2C0_t = undefined;
var tbl_n: c_int = 0;

pub const stubs = struct {
    pub fn func_8008D2C0(ctx: *zd.Ctx, out: anytype) c_int {
        _ = ctx;
        const dst: [*]v.func_8008D2C0_t = @ptrCast(@alignCast(out));
        for (tbl, 0..) |ent, i| dst[i] = ent;
        return tbl_n;
    }
};

fn place(ctx: *zd.Ctx, base: i16, mode: u8) i16 {
    const b: i32 = base;
    const x: i32 = switch (mode) {
        0 => b + 0x40,
        1 => b - 0x40,
        2 => b + 0x41,
        3 => b - 0x41,
        4 => b,
        else => @as(i32, ctx.int(i16)),
    };
    return @intCast(std.math.clamp(x, -32768, 32767));
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const sv = ctx.alloc(v.SVECTOR);
    tbl_n = ctx.range(c_int, 1, 4);
    for (&tbl) |*ent| {
        ent.unk0 = ctx.pick(i16, &.{ 0, 64, -64, 32767, -32768, 32703, -32704, -1 });
        ent.unk2 = ctx.int(i16);
        ent.unk4 = ctx.pick(i16, &.{ 0, 64, -64, 32767, -32768, 32703, -32704, -1 });
        ent.unk6 = ctx.pick(i16, &.{ 0, 1, -1, 2, 32767, -32768 });
    }
    const k = ctx.range(usize, 0, @intCast(tbl_n - 1));
    const ent = tbl[k];
    // The entry's unk6 + 2 matches it (the C's loop takes the first match).
    const arg0: c_int = @as(c_int, ent.unk6) + 2;

    const mode_x = ctx.range(u8, 0, 5);
    const mode_z = ctx.range(u8, 0, 5);
    sv.vx = place(ctx, ent.unk0, mode_x);
    sv.vy = ctx.int(i16);
    sv.vz = place(ctx, ent.unk4, mode_z);
    sv.pad = ctx.int(i16);
    return .{ arg0, sv };
}

/// The index the C's loop finds for this arg0 (always present).
fn found(arg0: c_int) usize {
    const n: usize = @intCast(tbl_n);
    for (0..n) |i| {
        if (@as(c_int, tbl[i].unk6) == arg0 - 2) return i;
    }
    unreachable;
}

pub fn classify(args: Args) usize {
    const sv = args[1].?;
    const p = tbl[found(args[0])];
    const x: i32 = sv.vx;
    const z: i32 = sv.vz;
    if (@as(i32, p.unk0) + 0x40 < x) return 0;
    if (x < @as(i32, p.unk0) - 0x40) return 1;
    if (@as(i32, p.unk4) + 0x40 < z) return 2;
    if (z < @as(i32, p.unk4) - 0x40) return 3;
    return 4;
}
