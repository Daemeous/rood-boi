//! zig_diff spec: func_800D61AC -> func_800D61AC.func_800D61AC (5BF94.c).
//! Inputs: a command as func_800D6298's spec builds it; D_800F5230's target
//! count 0, 1, 2 or 16 (its unkD0 holds 16); func_800A0BE0 returns any word
//! (bit 1 set or clear per target).
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D61AC));

pub fn command(ctx: *zd.Ctx, script: *v.D_800F53B8_t) *v.func_800D5780_t {
    const cmd = ctx.alloc(v.func_800D5780_t);
    const words: [*]u32 = @ptrCast(@alignCast(ctx.allocBytes(32 * 8, 16)));
    psx.put(&cmd.unk0, @as(*u32, @ptrCast(words)));
    cmd.unk6 = ctx.pick(i16, &.{ 0, 1, 31 });
    if (cmd.unk6 < 0 or cmd.unk6 > 31) cmd.unk6 = @intCast(@mod(cmd.unk6, 32));
    const t4 = ctx.alloc(v.D_800F53B8_t4);
    psx.put(&script.unkD1C.unk30, t4);
    const entry: *v.func_800D6310_t = @ptrCast(@alignCast(words + 2 * @as(usize, @intCast(cmd.unk6))));
    if (ctx.random.boolean()) t4.unk2 = entry.unk0;
    return cmd;
}

fn entryOf(cmd: *v.func_800D5780_t) *v.func_800D6310_t {
    const words: [*]u32 = @ptrCast(psx.get(cmd.unk0).?);
    return @ptrCast(@alignCast(words + 2 * @as(usize, @intCast(cmd.unk6))));
}

fn atFrame(args: Args) bool {
    return entryOf(args[1].?).unk0 == psx.get(args[0].?.unkD1C.unk30).?.unk2;
}

pub const classes = [_][:0]const u8{ "at-frame-none", "at-frame-targets", "other-frame" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const s = ctx.alloc(v.D_800F53B8_t);
    v.g.D_800F5230().unk3 = ctx.edge(u8, &.{ 0, 1, 2, 16 });
    return .{ s, command(ctx, s) };
}

pub fn classify(args: Args) usize {
    if (!atFrame(args)) return 2;
    return if (v.g.D_800F5230().unk3 == 0) 0 else 1;
}
