//! zig_diff spec: func_800D5E00 -> func_800D5E00.func_800D5E00 (5BF94.c).
//! Inputs: a command over 32 random u16 entries, its index 0..31, the
//! entry's 9-bit frame equal to the script's half the time and its kind
//! (bits 14..15) any; the script's unk0 / unk18 targets kind 4 or not, their
//! id bytes 0 or not; the P-file's sound targets 0..3 of them (kind 4 with
//! id byte 0 sometimes) and a sound block with random i16 offsets.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D5E00));

pub const classes = [_][:0]const u8{ "other-frame", "always", "unk0-target", "unk18-target", "any-target" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const s = ctx.alloc(v.D_800F53B8_t);
    const cmd = ctx.alloc(v.func_800D5780_t);
    const data: [*]u16 = @ptrCast(@alignCast(ctx.allocBytes(64, 16)));
    psx.put(&cmd.unk0, @as(*u32, @ptrCast(@alignCast(data))));
    cmd.unk6 = ctx.range(i16, 0, 31);
    const t4 = ctx.alloc(v.D_800F53B8_t4);
    psx.put(&s.unkD1C.unk30, t4);
    t4.unk2 = ctx.pick(i16, &.{ 0, 1, 511, 512, -1 });
    const k: usize = @intCast(cmd.unk6);
    if (ctx.random.boolean()) data[k] = (data[k] & 0xFE00) | (@as(u16, @bitCast(t4.unk2)) & 511);
    for ([_]*v.D_800F53B8_t3_2{ &s.unkD1C.unk0, &s.unkD1C.unk18 }) |t| {
        t.unk0 = ctx.pick(u16, &.{ 4, 4, 5 });
        t.unk4.unk0 = ctx.pick(i32, &.{ 0, 0, 256, 1, -1 });
    }
    const pf = ctx.alloc(v.D_800F569C_t);
    const targets: *v.soundEventTargets = @ptrCast(@alignCast(ctx.allocBytes(16 + 3 * 12, 16)));
    targets.count = ctx.range(u8, 0, 3);
    const list: [*]v.D_800F53B8_t3_2 = @ptrCast(&targets.target);
    for (0..3) |i| {
        list[i].unk0 = ctx.pick(u16, &.{ 4, 5 });
        list[i].unk4.unk0 = ctx.pick(i32, &.{ 0, 256, 1 });
    }
    psx.put(&pf.unkD0, @as(*anyopaque, @ptrCast(targets)));
    psx.put(&pf.block11Data, @as(*u8, @ptrCast(ctx.allocBytes(4 + 64, 16))));
    v.g.D_800F569C().* = pf;
    return .{ s, cmd };
}

pub fn classify(args: Args) usize {
    const data: [*]u16 = @ptrCast(@alignCast(psx.get(args[1].?.unk0).?));
    const p = data[@intCast(args[1].?.unk6)];
    if (@as(c_int, p & 511) != psx.get(args[0].?.unkD1C.unk30).?.unk2) return 0;
    return 1 + (p >> 14);
}
