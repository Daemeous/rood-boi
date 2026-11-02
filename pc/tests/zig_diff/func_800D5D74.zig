//! zig_diff spec: func_800D5D74 -> func_800D5D74.func_800D5D74 (5BF94.c).
//! Inputs: a command whose word list is 32 random 8-byte entries (its
//! address taken in 32-bit arithmetic, as the C does), the index 0..31; the
//! entry's 9-bit frame equal to the script's half the time; the renderer
//! table keeps the harness's random words (passed through to the stub).
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D5D74));

pub const classes = [_][:0]const u8{ "at-frame", "other-frame" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const s = ctx.alloc(v.D_800F53B8_t);
    const cmd = ctx.alloc(v.func_800D5780_t);
    const words: [*]u32 = @ptrCast(@alignCast(ctx.allocBytes(32 * 8, 16)));
    psx.put(&cmd.unk0, @as(*u32, @ptrCast(words)));
    cmd.unk6 = ctx.range(i16, 0, 31);
    const t4 = ctx.alloc(v.D_800F53B8_t4);
    psx.put(&s.unkD1C.unk30, t4);
    t4.unk2 = ctx.pick(i16, &.{ 0, 1, 511, 512, -1 });
    const entry: *v.func_800D5D74_t = @ptrCast(@alignCast(words + 2 * @as(usize, @intCast(cmd.unk6))));
    if (ctx.random.boolean()) psx.setBits(entry, "unk0_0", @truncate(@as(u16, @bitCast(t4.unk2))));
    return .{ s, cmd };
}

pub fn classify(args: Args) usize {
    const words: [*]u32 = @ptrCast(psx.get(args[1].?.unk0).?);
    const entry: *v.func_800D5D74_t = @ptrCast(@alignCast(words + 2 * @as(usize, @intCast(args[1].?.unk6))));
    return if (@as(c_int, psx.getBits(entry, "unk0_0")) == psx.get(args[0].?.unkD1C.unk30).?.unk2) 0 else 1;
}
