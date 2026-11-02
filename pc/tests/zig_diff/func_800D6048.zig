//! zig_diff spec: func_800D6048 -> func_800D6048.func_800D6048 (5BF94.c).
//! Inputs: a command over 32 random 8-byte entries, its index 0..31, the
//! frame equal half the time; `all` 0 or not; D_800F5230's target count 0,
//! 1, 2 or 16 (its unk10 holds 16), the targets' kinds 4 or not; the
//! script's unk24 target kind 4 or not; func_800A0BE0 returns any word.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D6048));

pub const classes = [_][:0]const u8{ "other-frame", "all", "own" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const s = ctx.alloc(v.D_800F53B8_t);
    const cmd = ctx.alloc(v.func_800D5780_t);
    const words: [*]u32 = @ptrCast(@alignCast(ctx.allocBytes(32 * 8, 16)));
    psx.put(&cmd.unk0, @as(*u32, @ptrCast(words)));
    cmd.unk6 = ctx.range(i16, 0, 31);
    const t4 = ctx.alloc(v.D_800F53B8_t4);
    psx.put(&s.unkD1C.unk30, t4);
    const entry: *v.func_800D6310_t = @ptrCast(@alignCast(words + 2 * @as(usize, @intCast(cmd.unk6))));
    if (ctx.random.boolean()) t4.unk2 = entry.unk0;
    const effect: *v.func_800D6048_t = @ptrCast(v.g.D_800F5230());
    v.g.D_800F5230().unk2 = ctx.edge(u8, &.{ 0, 1, 2, 16 });
    for (0..16) |i| effect.unk10[i].unk0 = ctx.pick(u16, &.{ 4, 4, 5 });
    s.unkD1C.unk24.unk0 = ctx.pick(u16, &.{ 4, 4, 5 });
    return .{ s, cmd, ctx.pick(c_int, &.{ 0, 0, 1, -1 }) };
}

pub fn classify(args: Args) usize {
    const words: [*]u32 = @ptrCast(psx.get(args[1].?.unk0).?);
    const entry: *v.func_800D6310_t = @ptrCast(@alignCast(words + 2 * @as(usize, @intCast(args[1].?.unk6))));
    if (entry.unk0 != psx.get(args[0].?.unkD1C.unk30).?.unk2) return 0;
    return if (args[2] != 0) 1 else 2;
}
