//! zig_diff spec: func_80090C2C -> func_80090C2C.func_80090C2C (2842C.c).
//! Inputs: the room (section B null 1 time in 6, else an arena block whose
//! unk48[1] extents are random or at the division edges: +-127, +-128, the
//! extremes); the weather kind -1..5 (and large values); the current sound
//! D_800F1DBC as -1, -2, 21, 0 or random (the four outcomes of the sound
//! switch). vs_battle_clamp is a recording stub that sometimes returns a
//! value above 16 bits (the C's (short) cast). func_8009121C and
//! func_80090B28 rewrite D_800F1DBC, so a port that reads it before them is
//! caught.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80090C2C));

pub const classes = [_][:0]const u8{ "no-room-playing", "no-room-silent", "kind-0", "kind-1", "kind-2", "kind-3", "kind-4", "kind-other" };

var sound_after: i16 = 0;
var rewrite: bool = false;

pub const stubs = struct {
    pub fn func_80090B28(ctx: *zd.Ctx) void {
        _ = ctx;
        if (rewrite) v.g.D_800F1DBC().* = sound_after;
    }
    pub fn vs_battle_clamp(ctx: *zd.Ctx, x: anytype, lo: anytype, hi: anytype) c_int {
        _ = x;
        _ = lo;
        _ = hi;
        return if (ctx.value(u8) & 3 == 0) ctx.value(c_int) else ctx.value(u8);
    }
};

fn sound(ctx: *zd.Ctx) i16 {
    return switch (ctx.range(u8, 0, 9)) {
        0, 1 => -1,
        2 => -2,
        3, 4 => 21,
        5 => 0,
        6 => -32768,
        else => ctx.int(i16),
    };
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const data = v.g.vs_battle_roomData();
    if (ctx.chance(6)) {
        psx.put(&data.sectionB, null);
    } else {
        const room = ctx.alloc(v._mpdRoomSectionB);
        for (0..2) |i| room.unk48[1].s16[i] = ctx.pick(i16, &.{ 127, 128, -127, -128, 255, 256, -32768, 32767, 0 });
        psx.put(&data.sectionB, room);
    }
    v.g.D_800F1DBC().* = sound(ctx);
    rewrite = ctx.chance(4);
    sound_after = sound(ctx);
    const kind = ctx.pick(c_int, &.{ 0, 1, 2, 3, 4, 5, -1, 1, 2, 3, 4 });
    return .{kind};
}

pub fn classify(args: Args) usize {
    if (psx.get(v.g.vs_battle_roomData().sectionB) == null) {
        return if (v.g.D_800F1DBC().* >= 0) 0 else 1;
    }
    return switch (args[0]) {
        0...4 => @intCast(2 + args[0]),
        else => 7,
    };
}
