//! zig_diff spec: func_800AE828 -> func_800AE828.func_800AE828 (44F14.c).
//! Inputs: a random D_800F4538_t in the arena. Its animationId is picked
//! around the 0xCA..0xDB skip window (0xDC gives the equal case), unk6E4 is
//! zero or not (the floor override), the tile and floor bytes are random, and
//! the int at byte offset 1452 (the flag test) is drawn from edge values:
//! 0x0FFFFFFF and 0x10000000 (the equal case of the masked compare), 0xF0000000,
//! 0x80000000, 0 and 0x7FFFFFFF, so the sign bit and the other high bits are
//! each seen alone and together. arg0 is 0, 1, 2 or -1; arg2 covers 0x40-0x43 and 0.
//! D_800F45E0 points at one arena table object whose unk6C[8].actorId is
//! random. func_800A91DC returns a table index 0-15; func_8008B764 returns null
//! or a fresh room with a random 5-bit unk0_0; func_8008EB04 writes sp10 in
//! 0-7 and sp14 in 3-10, so the equal cases of sp10 < 3, sp10 > 0 and
//! sp14 >= 6 all occur.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_44F14");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800AE828));

pub const classes = [_][:0]const u8{ "anim-skip", "arg0-skip", "table-path", "room-path" };

pub const stubs = struct {
    pub fn func_800A91DC(ctx: *zd.Ctx, x: anytype, z: anytype, flag: anytype) c_int {
        _ = x;
        _ = z;
        _ = flag;
        return @as(c_int, ctx.value(u4));
    }

    pub fn func_8008B764(ctx: *zd.Ctx, x: anytype, z: anytype, floor: anytype) ?*v._mpdRoomSection3 {
        _ = x;
        _ = z;
        _ = floor;
        if (ctx.value(u8) < 96) return null;
        const r = ctx.alloc(v._mpdRoomSection3);
        psx.setBits(r, "unk0_0", ctx.value(u5));
        return r;
    }

    pub fn func_8008EB04(ctx: *zd.Ctx, a: anytype, b: anytype) void {
        const pa: *c_int = @ptrCast(@alignCast(a));
        const pb: *c_int = @ptrCast(@alignCast(b));
        pa.* = @as(c_int, ctx.value(u3));
        pb.* = @as(c_int, ctx.value(u3)) + 3;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg0 = ctx.pick(c_int, &.{ 0, 1, 2, -1 });
    const arg2 = ctx.pick(c_int, &.{ 0x40, 0x41, 0x42, 0x43, 0, -1 });

    const actor = ctx.alloc(v.D_800F4538_t);
    actor.animationId = ctx.pick(u8, &.{ 0xC9, 0xCA, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xE1, 0x00, 0xFF });
    actor.unk6E4 = if (ctx.chance(2)) 0 else ctx.pick(u8, &.{ 1, 0x80, 0xFF });
    actor.unk0.currentTileX = ctx.int(u8);
    actor.unk0.currentTileZ = ctx.int(u8);
    actor.unk0.unk5D = ctx.int(u8);
    // The int at byte offset 1452 (little endian), built from edge values so
    // the masked compare sees exact equality with 0x0FFFFFFF.
    const flag_word = ctx.pick(u32, &.{ 0x0FFFFFFF, 0x10000000, 0xF0000000, 0x80000000, 0x00000000, 0x7FFFFFFF });
    std.mem.writeInt(u32, actor._bits_1452[0..4], flag_word, .little);

    const table_obj = ctx.alloc(v.D_800F45E0_t);
    psx.setBits(&table_obj.unk6C[8], "actorId", ctx.range(u3, 0, 7));
    const tbl = v.g.D_800F45E0();
    for (tbl) |*e| e.* = table_obj;

    return .{ arg0, actor, arg2 };
}

pub fn classify(args: Args) usize {
    const a = args[1].?;
    if (a.animationId -% 0xCA < 18) return 0;
    if (args[0] == 1) return 1;
    const flag = std.mem.readInt(u32, a._bits_1452[0..4], .little);
    if ((flag & 0xF0000000) != 0) return 2;
    return 3;
}
