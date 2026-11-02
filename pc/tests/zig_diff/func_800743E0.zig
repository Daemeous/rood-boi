//! zig_diff spec: func_800743E0 -> func_800743E0.func_800743E0 (146C.c).
//! Inputs: D_800F1910.unk0 names an arena section A entry followed by the
//! loot it hands on; keyOrSigil 0..4 and -1 (outside 1..3 the C passes its
//! message id uninitialised: excluded), unk10 around 0x40 and at the i16
//! edges, unkE random.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800743E0));

pub const classes = [_][:0]const u8{ "key", "sigil", "both", "unknown-lock", "loot", "item" };

var lock: i16 = 0;

pub fn uninitialisedArgument(call: []const u8, index: usize) bool {
    return index == 0 and std.mem.eql(u8, call, "vs_battle_displaySceneMessage") and (lock < 1 or lock > 3);
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const raw = ctx.allocBytes(@sizeOf(v._mpdRoomSectionA) + @sizeOf(v.vs_battle_loot), 16);
    const section: *v._mpdRoomSectionA = @ptrCast(@alignCast(raw));
    lock = ctx.edge(i16, &.{ 0, 0, 0, 1, 2, 3, 4, -1 });
    section.keyOrSigil = lock;
    section.unk10 = ctx.pick(i16, &.{ 0, 0x3F, 0x40, 0x41, -1, 0x7FFF, -0x8000 });
    psx.put(&v.g.D_800F1910().unk0, section);
    return .{};
}

pub fn classify(_: Args) usize {
    const section = psx.get(v.g.D_800F1910().unk0).?;
    return switch (section.keyOrSigil) {
        0 => if (section.unk10 < 0x40) 4 else 5,
        1 => 0,
        2 => 1,
        3 => 2,
        else => 3,
    };
}
