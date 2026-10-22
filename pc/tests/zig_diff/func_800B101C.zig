//! zig_diff spec: func_800B101C -> func_800B101C.func_800B101C (44F14.c).
//! Inputs: one objectMotionContext in the arena, with a mode picked from the six
//! motion modes (0xFD, 0xFB, 0xFA, 0xF9, 0xF8, 0xF7) or a random i16, and a frame
//! kept inside the length of every table that mode reads (16, 12, 4 entries), so
//! frame 0 (which the C turns into 1), the completing frames (14, 9, 3, 40) and
//! the frame >= 9 test are all reached. Orientation takes the edges 0, 1, 2, 3,
//! negatives and the i16 extremes, plus random values. The random arena and
//! scratchpad bytes give signed and unsigned table reads and wrapping i16 stores.
//! vs_main_memcpy really copies (the C reads its effect back through the state
//! bytes); func_800A0104 and func_80099D6C keep the default stubs, since their
//! return values are discarded and the C reads nothing back from them.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_44F14");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B101C));

pub const classes = [_][:0]const u8{ "fd", "fb", "fa", "f9", "f8", "f7", "other" };

pub const stubs = struct {
    pub fn vs_main_memcpy(ctx: *zd.Ctx, dest: anytype, src: anytype, sz: anytype) void {
        _ = ctx;
        const n: usize = @intCast(sz);
        const d: [*]u8 = @ptrCast(dest.?);
        const s: [*]const u8 = @ptrCast(src.?);
        @memcpy(d[0..n], s[0..n]);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj = ctx.alloc(v.objectMotionContext);

    const sel = ctx.range(u8, 0, 9);
    const modes = [_]i16{ 0xFD, 0xFB, 0xFA, 0xF9, 0xF8, 0xF7 };
    obj.mode = if (sel < modes.len) modes[sel] else ctx.int(i16);

    // Every table the mode reads has this many entries; frame stays below it.
    const limit: u8 = switch (obj.mode) {
        0xFD, 0xF9, 0xF7 => 16,
        0xFB => 12,
        0xFA => 4,
        else => 0,
    };
    obj.frame = if (limit == 0)
        ctx.pick(u8, &.{ 0, 1, 2, 3, 8, 9, 10, 14, 39, 40, 41, 255 })
    else
        ctx.range(u8, 0, limit - 1);

    obj.orientation = ctx.pick(i16, &.{ 0, 1, 2, 3, -1, -2, 4, 32767, -32768 });

    return .{obj};
}

pub fn classify(args: Args) usize {
    const obj = args[0].?;
    return switch (obj.mode) {
        0xFD => 0,
        0xFB => 1,
        0xFA => 2,
        0xF9 => 3,
        0xF8 => 4,
        0xF7 => 5,
        else => 6,
    };
}
