//! zig_diff spec: func_800D57FC -> func_800D57FC.func_800D57FC (5BF94.c).
//! Inputs: a command over 32 random 4-byte entries, its index 0..31, the
//! script's frame one before, at or after the entry's; unkA 1, 2 or 0; the
//! script paused (unk1 bit 0) or not; unk8 any.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D57FC));

pub fn command(ctx: *zd.Ctx, comptime E: type) struct { *v.D_800F53B8_t, *v.func_800D5780_t } {
    const s = ctx.alloc(v.D_800F53B8_t);
    const cmd = ctx.alloc(v.func_800D5780_t);
    const e: [*]E = @ptrCast(@alignCast(ctx.allocBytes(32 * @sizeOf(E), 16)));
    psx.put(&cmd.unk0, @as(*u32, @ptrCast(@alignCast(e))));
    cmd.unk6 = ctx.range(i16, 0, 31);
    cmd.unkA = ctx.pick(i16, &.{ 1, 1, 2, 0 });
    const t4 = ctx.alloc(v.D_800F53B8_t4);
    psx.put(&s.unkD1C.unk30, t4);
    const key: c_int = psx.getBits(&e[@intCast(cmd.unk6)], "unk0_0");
    t4.unk2 = @intCast(switch (ctx.range(u8, 0, 2)) {
        0 => key,
        1 => key - 1,
        else => key + 1,
    });
    return .{ s, cmd };
}

fn keyOf(comptime E: type, args: Args) c_int {
    const e: [*]E = @ptrCast(@alignCast(psx.get(args[1].?.unk0).?));
    return psx.getBits(&e[@intCast(args[1].?.unk6)], "unk0_0");
}

pub const classes = [_][:0]const u8{ "before", "running", "paused" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const r = command(ctx, v.func_800D57FC_t);
    return .{ r[0], r[1] };
}

pub fn classify(args: Args) usize {
    const key = keyOf(v.func_800D57FC_t, args);
    const t4 = psx.get(args[0].?.unkD1C.unk30).?;
    if (t4.unk2 < key) return 0;
    return if ((t4.unk1 & 1) == 0) 1 else 2;
}
