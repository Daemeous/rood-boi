//! zig_diff spec: func_800C253C -> func_800C253C.func_800C253C (58578.c).
//! Inputs: `type` over the four ranges the C takes (0; 1 and 2; 3..5 and
//! negative values through the unsigned test). The shape word sits in the
//! range its table index allows (0 included, any sign in the else branch),
//! the vertex-count stubs advance the index, and the tables are filled so
//! every command reads inside its table. The `i == 0` test is on a 5-bit
//! field, so it never sees a sign bit: that flag is unreachable from inputs.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_58578");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800C253C));

pub const classes = [_][:0]const u8{ "type-zero", "type-one-two", "type-negative", "type-three-to-five" };

pub const stubs = struct {
    /// Returns the index advanced by a few vertices (both runs draw the same value).
    pub fn func_800C0758(ctx: *zd.Ctx, phase: anytype, segments: anytype, index: anytype) c_int {
        _ = phase;
        _ = segments;
        return @as(c_int, index) + @as(c_int, ctx.value(u3));
    }

    pub fn func_800C2368(ctx: *zd.Ctx, step: anytype, radius: anytype, index: anytype) c_int {
        _ = step;
        _ = radius;
        return @as(c_int, index) + @as(c_int, ctx.value(u3));
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    // The C reads D_800EB9B8->unk2A and writes the vertex array in the struct.
    const s = ctx.alloc(v.D_800EB9B8_t);
    v.g.D_800EB9B8().* = s;

    const types = [_]c_int{ 0, 0, 1, 1, 2, 3, 4, 5, -1, -2, -20, -45 };
    const t = types[ctx.range(usize, 0, types.len - 1)];

    // shape = unk2A, and shape (+ 16 * type) indexes a 52-entry table.
    s.unk2A = switch (t) {
        0 => ctx.range(i16, 0, 50),
        1 => ctx.range(i16, 0, 34),
        2 => ctx.range(i16, 0, 18),
        else => ctx.pick(i16, &.{ 0, -1, 1, 32767, -32768 }),
    };
    s.unk24 = ctx.pick(i16, &.{ 32767, -32768, -1, 0 });
    s.unk26 = ctx.pick(i16, &.{ 32767, -32768, -1, 0 });
    s.unk28 = ctx.pick(i16, &.{ 32767, -32768, -1, 0 });

    // Command ranges: D_800EA438 entries are at most 255, so every command
    // read lies in D_800EA46C[0..254]. Equal neighbours make count == 0 common.
    const sh = v.g.D_800EA438();
    for (0..52) |k| {
        sh[k] = if (k > 0 and ctx.chance(2)) sh[k - 1] else ctx.int(u8);
    }

    // D_800EA46C commands: case 0 needs (command & 0x1F) <= 17 so the
    // D_800EA234 read stays below 72; case 2 needs (command & 0x3FF) <= 159
    // so the D_800EA2C4 read stays below 186.
    const cmds = v.g.D_800EA46C();
    for (0..258) |k| {
        var c = ctx.int(u16);
        switch (c >> 14) {
            0 => c = (c & 0xFFE0) | ctx.range(u16, 0, 17),
            2 => c = (c & 0xFC00) | ctx.range(u16, 0, 159),
            else => {},
        }
        cmds[k] = c;
    }

    const a = v.g.D_800EA234();
    for (0..72) |k| a[k] = ctx.pick(i16, &.{ 0, -1, 32767, -32768 });
    const b = v.g.D_800EA2C4();
    for (0..186) |k| b[k] = ctx.pick(i16, &.{ 0, -1, 32767, -32768 });

    return .{t};
}

pub fn classify(args: Args) usize {
    const t = args[0];
    if (t == 0) return 0;
    if (t == 1 or t == 2) return 1;
    if (t < 0) return 2;
    return 3;
}
