//! zig_diff spec: func_8009D6F4 -> func_8009D6F4.func_8009D6F4 (30DB0.c).
//! Inputs: the 17-entry actor table D_800F4538 with each slot null or a
//! fresh actor from the arena. Each run picks a mode: all slots null (the
//! null-continue path only), every live actor's unk5AC_3 bit clear, or a
//! random bit per actor. Random fill leaves every other field nonzero, so a
//! dropped or misplaced store shows in the state compare.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009D6F4));

pub const classes = [_][:0]const u8{ "no-actors", "bit-set", "bit-clear" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const tbl = v.g.D_800F4538();
    const mode = ctx.range(u8, 0, 9);
    for (tbl) |*slot| {
        if (mode == 0 or ctx.chance(5)) {
            slot.* = null;
            continue;
        }
        const actor = ctx.alloc(v.D_800F4538_t);
        psx.setBits(actor, "unk5AC_3", if (mode == 1) @as(u1, 0) else ctx.range(u1, 0, 1));
        slot.* = actor;
    }
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    var any = false;
    for (v.g.D_800F4538().*) |slot| {
        const actor = slot orelse continue;
        any = true;
        if (psx.getBits(actor, "unk5AC_3") != 0) return 1;
    }
    return if (any) 2 else 0;
}
