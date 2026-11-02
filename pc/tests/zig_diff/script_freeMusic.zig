//! zig_diff spec: vs_battle_script_freeMusic -> script_free_music.script_freeMusic (4A0A8.c).
//! Inputs: a two-byte argument buffer in the arena whose second byte is random
//! (so the byte passed to vs_main_freeMusic is sometimes 0x80 and up, which a
//! signed read would turn negative), and the global D_800F4C2C set to 2 about
//! half the time, otherwise random. The stub of vs_main_freeMusic rewrites
//! D_800F4C2C sometimes (to 2 or to any byte), so a port that reads the global
//! before the call, or keeps a copy, returns a different value.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").script_freeMusic));

pub const classes = [_][:0]const u8{ "high-byte", "global-is-two", "other" };

pub const stubs = struct {
    pub fn vs_main_freeMusic(ctx: *zd.Ctx, arg0: anytype) c_int {
        _ = arg0;
        // The callee can change the global the C reads back after the call.
        const sel = ctx.value(u2);
        if (sel == 0) {
            v.g.D_800F4C2C().* = 2;
        } else if (sel == 1) {
            v.g.D_800F4C2C().* = ctx.value(u8);
        }
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf: [*c]u8 = @as([*c]u8, ctx.allocBytes(2, 1));
    // Second byte: half the time forced into 0x80..0xFF (signed-read edge).
    if (ctx.chance(2)) buf[1] = ctx.range(u8, 0x80, 0xFF);
    const g = v.g.D_800F4C2C();
    if (ctx.chance(2)) {
        g.* = 2;
    } else {
        g.* = ctx.pick(u8, &.{ 0, 1, 3, 0x42, 0xFF });
    }
    return .{ buf, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    if (args[0][1] >= 0x80) return 0;
    if (v.g.D_800F4C2C().* == 2) return 1;
    return 2;
}
