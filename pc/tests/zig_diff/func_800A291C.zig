//! zig_diff spec: func_800A291C -> func_800A291C.func_800A291C (38C1C.c).
//! Inputs: a D_800F4538_t in the arena whose position and the addend unk1848.unk10
//! are picked from edge values (0, +-1, +-127/128, +-255/256, +-32767/-32768), with
//! sometimes x or z at the i16 limits so that the add wraps. The divisions by 128
//! see negative positions (truncation toward zero, u8 wrap of the tile index), and
//! unk6, unkF are random, so the call's arguments vary. The C has no branches; its
//! value edges are the i16 sums and the signed division.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_38C1C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A291C));

pub const classes = [_][:0]const u8{ "wrap", "negative", "nonnegative" };

const edges = [_]i16{ 0, 1, -1, 127, 128, -127, -128, 255, 256, -255, -256, 32767, -32768, 32766, -32767 };

pub fn prepare(ctx: *zd.Ctx) Args {
    const actor = ctx.alloc(v.D_800F4538_t);
    actor.unk0.position.vx = ctx.pick(i16, &edges);
    actor.unk0.position.vy = ctx.pick(i16, &edges);
    actor.unk0.position.vz = ctx.pick(i16, &edges);
    actor.unk1848.unk10.vx = ctx.pick(i16, &edges);
    actor.unk1848.unk10.vy = ctx.pick(i16, &edges);
    actor.unk1848.unk10.vz = ctx.pick(i16, &edges);
    if (ctx.chance(3)) {
        actor.unk0.position.vx = ctx.pick(i16, &.{ 32767, -32768 });
        actor.unk1848.unk10.vx = ctx.pick(i16, &.{ 1, 32767, -1, -32768 });
    }
    if (ctx.chance(3)) {
        actor.unk0.position.vz = ctx.pick(i16, &.{ 32767, -32768 });
        actor.unk1848.unk10.vz = ctx.pick(i16, &.{ 1, 32767, -1, -32768 });
    }
    return .{actor};
}

pub fn classify(args: Args) usize {
    const actor = args[0].?;
    const p = actor.unk0.position;
    const d = actor.unk1848.unk10;
    const sx: i32 = @as(i32, p.vx) + @as(i32, d.vx);
    const sy: i32 = @as(i32, p.vy) + @as(i32, d.vy);
    const sz: i32 = @as(i32, p.vz) + @as(i32, d.vz);
    const lo: i32 = -32768;
    const hi: i32 = 32767;
    if (sx < lo or sx > hi or sy < lo or sy > hi or sz < lo or sz > hi) return 0;
    return if (sx < 0) 1 else 2;
}
