//! zig_diff spec: func_800AA490 -> func_800AA490.func_800AA490 (40564.c).
//! Inputs: a slot of D_800F4538 (null in a sixth of the runs, and then arg1 is
//! sometimes null too, since the C returns before reading it). The actor's tile
//! either equals the arg1 tile (the angle == 0 && dz == 0 branch) or not (the
//! ratan2 branch, whose stub returns ratan_ret most of the time). The facing is
//! chosen so the turn angle lands on the edges -2048, 2048 and their neighbours.
//! arg2 and arg3 come from edge lists: 0, -1 and other values for the three
//! arg2 paths, and negative arg3 for the sign of the product.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_40564");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800AA490));

pub const classes = [_][:0]const u8{ "no-actor", "same-tile", "arg2-zero", "arg2-minus1", "arg2-other" };

/// The value ratan2 returns in most calls (set by prepare).
var ratan_ret: c_int = 0;

pub const stubs = struct {
    pub fn ratan2(ctx: *zd.Ctx, y: anytype, x: anytype) c_int {
        _ = y;
        _ = x;
        if (ctx.value(u4) != 0) return ratan_ret;
        // Bounded, so 0xC00 - r cannot overflow in the C.
        return @as(c_int, ctx.value(i24));
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const actor = ctx.alloc(v.D_800F4538_t);
    const fields = ctx.alloc(v.func_8006EBF8_t_fields);
    const slot: c_int = ctx.range(c_int, 0, 16);
    const null_actor = ctx.chance(6);
    v.g.D_800F4538()[@intCast(slot)] = if (null_actor) null else actor;

    const arg2 = ctx.edge(c_int, &.{ 0, -1, 1, 2, -2, 65536, 0x7fff_ffff, -0x8000_0000 });
    const arg3 = ctx.edge(c_int, &.{ 0, 1, -1, 4095, 4096, -4096, 4097, -4097, 500000, -500000, 12345, -777 });

    const ax = ctx.int(u8);
    const az = ctx.int(u8);
    const b = ctx.int(u8);
    psx.setBits(fields, "unk0_0", ax);
    psx.setBits(fields, "unk0_16", az);
    psx.setBits(fields, "unk0_24", b);

    if (null_actor) {
        return .{ slot, if (ctx.chance(2)) null else fields, arg2, arg3 };
    }

    if (ctx.chance(3)) {
        actor.unk0.currentTileX = ax;
        actor.unk0.currentTileZ = az;
    } else {
        actor.unk0.currentTileX = ctx.int(u8);
        actor.unk0.currentTileZ = ctx.int(u8);
    }
    const same = actor.unk0.currentTileX == ax and actor.unk0.currentTileZ == az;

    ratan_ret = if (ctx.chance(4))
        ctx.range(c_int, -(1 << 20), 1 << 20)
    else
        ctx.edge(c_int, &.{ 0, 0xC00, 0xC01, 4096, -4096, 2048, 1000, -1000 });

    // The angle before the facing is subtracted: the tile angle, or ratan2's result mod 4096.
    const pre: c_int = if (same) @as(c_int, b) * 16 else @rem(@as(c_int, 0xC00) - ratan_ret, 4096);
    const target = ctx.edge(c_int, &.{ 0, 1, -1, 2047, 2048, -2047, -2048, 2049, -2049, 1000, -1000, 4000 });
    var facing: c_int = pre - target;
    if (ctx.chance(3)) facing = ctx.range(c_int, 0, 4095);
    actor.unk0.facing = @intCast(facing);

    return .{ slot, fields, arg2, arg3 };
}

pub fn classify(args: Args) usize {
    const actor = v.g.D_800F4538()[@intCast(args[0])] orelse return 0;
    const f = args[1].?;
    if (psx.getBits(f, "unk0_0") == actor.unk0.currentTileX and
        psx.getBits(f, "unk0_16") == actor.unk0.currentTileZ) return 1;
    return switch (args[2]) {
        0 => 2,
        -1 => 3,
        else => 4,
    };
}
