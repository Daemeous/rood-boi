//! zig_diff spec: func_800D49A4 -> func_800D49A4.func_800D49A4 (5BF94.c).
//! Inputs: an arg0 object in the arena, sized for 256 unk1C elements (the loop
//! bound is a u8, so it may write well past the 16 declared ones). Its unk38
//! covers the u_short window 6..21 (with the 22 equality and the -1 wrap), the
//! 0x21 and 0x24 selectors, and 0x38 with unk10[3] == 0x2B or not. The loop
//! count is the block9 field unk15 at 0, 1, 2, 15, 16, 17, 255 or any u8. The
//! block9 pointer and D_800F569C are set here, since the C dereferences both.
//! The function makes no calls, so there are no stubs.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D49A4));

pub const classes = [_][:0]const u8{ "window-6-21", "selector-21-24", "store-0x38-2B", "other" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const size = @offsetOf(v.D_800F53B8_t, "unk1C") + 256 * @sizeOf(v.func_800CE714_t2_2);

    // The global block table: D_800F569C -> block9Data -> pFileBlock9.
    const blk = ctx.alloc(v.pFileBlock9);
    blk.unk15 = ctx.pick(u8, &.{ 0, 1, 2, 15, 16, 17, 255 });
    const top = ctx.alloc(v.D_800F569C_t);
    psx.put(&top.block9Data, blk);
    const d = v.g.D_800F569C();
    d.* = top;

    const arg: *v.D_800F53B8_t = @ptrCast(@alignCast(ctx.allocBytes(size, 16)));
    arg.unkD1C.unk38 = ctx.pick(i16, &.{ 5, 6, 7, 21, 22, -1, 0, 0x21, 0x24, 0x38, -32768, 32767 });
    arg.unk10[3] = if (ctx.chance(2)) 0x2B else ctx.int(u8);
    arg.unk10[2] = if (ctx.chance(4)) 0x2B else ctx.int(u8);
    return .{arg};
}

pub fn classify(args: Args) usize {
    const arg = args[0].?;
    const t = arg.unkD1C.unk38;
    if (@as(u16, @bitCast(t -% 6)) < 16) return 0;
    if (t == 0x21 or t == 0x24) return 1;
    if (t == 0x38 and arg.unk10[3] == 0x2B) return 2;
    return 3;
}
