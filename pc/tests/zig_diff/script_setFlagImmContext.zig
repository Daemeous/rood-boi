//! zig_diff spec: vs_battle_script_setFlagImmContext -> script_set_flag_imm_context.script_setFlagImmContext (4A0A8.c).
//! Inputs: arg0 is either a block in the arena or the PS1 address-0 region (both valid memory; the C only forms arg0 + 1 and passes it on), and arg1 is a short with edge values, which the C ignores. The callees are default recording stubs: vs_battle_getShort returns any short, so bytes above 255 reach vs_battle_getStateFlag and the u8 store. The C has no branches, so the two classes only name which kind of arg0 the run used: "arena-arg0" and "low-ram-arg0".
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").script_setFlagImmContext));

pub const classes = [_][:0]const u8{ "arena-arg0", "low-ram-arg0" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg0: [*c]u8 = if (ctx.chance(4))
        @ptrCast(psx.lowRam(u8))
    else
        @ptrCast(ctx.allocBytes(8, 16));
    const arg1 = ctx.pick(c_short, &.{ 0, 1, -1, 0x7FFF, -0x8000, 0x00FF, 0x0100 });
    return .{ arg0, arg1 };
}

pub fn classify(args: Args) usize {
    return if (@intFromPtr(args[0]) == zd.low_ram) 1 else 0;
}
