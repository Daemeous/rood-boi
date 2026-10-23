//! zig_diff spec: func_80093A14 -> func_80093A14.func_80093A14 (2842C.c).
//! Inputs: D_800F2258 is -1 (the idle case, often) or any i16 edge value, and
//! vs_battle_roomData.section14 is null or a fresh arena word, so both
//! operands of the && and the null test are exercised. The callee
//! func_80046494 is a recording stub that also writes a random value through
//! its pointer argument when that pointer is non-null, so a port that reads
//! that word back after the call is caught.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80093A14));

pub const classes = [_][:0]const u8{ "idle-D_800F2258", "null-section14", "call" };

pub const stubs = struct {
    pub fn func_80046494(ctx: *zd.Ctx, arg0: anytype, arg1: anytype, arg2: anytype, arg3: anytype, arg4: anytype) void {
        _ = arg0;
        _ = arg2;
        _ = arg3;
        _ = arg4;
        if (@intFromPtr(arg1) != 0) {
            const p: *u32 = @ptrCast(@alignCast(arg1));
            p.* = ctx.value(u32);
        }
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const d = v.g.D_800F2258();
    d.* = ctx.pick(i16, &.{ -1, -1, 0, 1, 0x7F, -2, -32768, 32767 });
    const room = v.g.vs_battle_roomData();
    if (ctx.chance(4)) {
        psx.put(&room.section14, @as(?*u32, null));
    } else {
        psx.put(&room.section14, ctx.alloc(u32));
    }
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    if (v.g.D_800F2258().* == -1) return 0;
    if (psx.get(v.g.vs_battle_roomData().section14) == null) return 1;
    return 2;
}
