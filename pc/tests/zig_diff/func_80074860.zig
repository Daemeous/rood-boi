//! zig_diff spec: func_80074860 -> func_80074860.func_80074860 (146C.c).
//! Inputs: any actor index. The stub of func_800A1108 fills the position
//! out-struct; the stub of func_80074798 writes the cell (the position's x,
//! y and z low bytes, and a random one) into the buffer it is given (on
//! 64-bit the block-scope static `sp20`, globalised: views
//! func_80074860__sp20, which prepare also scrambles); the stub of
//! func_800BEC58 reads the cell back through its int argument, as the script
//! does in mode 1, and returns 1 (an event), 0 (none), 2 (busy), or a value
//! whose low byte or sign alone would pass for 1 (257, -1, -255). `mode` (set
//! by prepare) picks the answer and is the input class.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80074860));

pub const classes = [_][:0]const u8{ "event", "none", "busy", "low-byte-one", "negative" };

var mode: usize = 0;

pub const stubs = struct {
    pub fn func_800A1108(ctx: *zd.Ctx, arg0: anytype, out: anytype) c_int {
        _ = arg0;
        const q: ?*anyopaque = out;
        const s: *v.func_8006EBF8_t = @ptrCast(@alignCast(q.?));
        s.unk0.unk4.vx = ctx.value(i16);
        s.unk0.unk4.vy = ctx.value(i16);
        s.unk0.unk4.vz = ctx.value(i16);
        s.unkC = ctx.value(i32);
        return ctx.value(c_int);
    }

    // The cell from the position it is given (x, y, z low bytes), the fourth
    // byte random.
    pub fn func_80074798(ctx: *zd.Ctx, position: anytype, out: anytype) c_int {
        const q: ?*anyopaque = position;
        const s: *const v.func_8006EBF8_t = @ptrCast(@alignCast(q.?));
        const b: [*]u8 = @ptrCast(out);
        b[0] = @truncate(@as(u16, @bitCast(s.unk0.unk4.vx)));
        b[1] = @truncate(@as(u16, @bitCast(s.unk0.unk4.vy)));
        b[2] = @truncate(@as(u16, @bitCast(s.unk0.unk4.vz)));
        b[3] = ctx.value(u8);
        return ctx.value(c_int);
    }

    // The script compares the cell's [0] and [2] in mode 1: read them through
    // the int, so a wrong buffer changes the answer as well as the argument.
    pub fn func_800BEC58(ctx: *zd.Ctx, kind: anytype, arg1: anytype, cell: anytype, call_mode: anytype) i16 {
        _ = ctx;
        _ = kind;
        _ = arg1;
        _ = call_mode;
        const b: [*]const u8 = @ptrFromInt(@as(usize, @as(u32, @bitCast(@as(c_int, cell)))));
        const salt: i16 = @intCast((b[0] ^ b[2]) & 1);
        return switch (mode) {
            0 => 1,
            1 => 0,
            2 => 2,
            3 => 257 + salt * 512,
            else => if (salt == 0) -1 else -255,
        };
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    mode = ctx.range(usize, 0, 4);
    if (comptime @sizeOf(usize) == 8) {
        for (v.g.func_80074860__sp20()) |*b| b.* = ctx.value(u8);
    }
    return .{ctx.int(c_int)};
}

pub fn classify(args: Args) usize {
    _ = args;
    return mode;
}
