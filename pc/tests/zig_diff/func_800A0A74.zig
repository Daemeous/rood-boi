//! zig_diff spec: func_800A0A74 -> func_800A0A74.func_800A0A74 (30DB0.c).
//! Inputs: an index into D_800F4538 from 0 to 16 (both ends favoured), with
//! the table entry either NULL or a random arena D_800F4538_t whose bits
//! unk5AC_9 and unk5AC_11 are random. The C has no calls, so there are no
//! stubs. The tested value `unk5AC_11 << 11` is 0 or 2048 and can never carry
//! the sign bit, so only the zero/nonzero split is reachable. On the NULL path
//! the C falls off the end, so the return value there is whatever the C leaves.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A0A74));

pub const classes = [_][:0]const u8{ "null-entry", "bit11-set", "bit11-clear-bit9-set", "bit11-clear-bit9-clear" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const table = v.g.D_800F4538();
    const idx: c_int = if (ctx.chance(2))
        ctx.edge(c_int, &.{ 0, 16 })
    else
        ctx.range(c_int, 0, 16);
    if (ctx.chance(4)) {
        table[@intCast(idx)] = null;
    } else {
        const obj = ctx.alloc(v.D_800F4538_t);
        psx.setBits(obj, "unk5AC_11", ctx.int(u1));
        psx.setBits(obj, "unk5AC_9", ctx.int(u1));
        table[@intCast(idx)] = obj;
    }
    return .{idx};
}

pub fn classify(args: Args) usize {
    const p = v.g.D_800F4538()[@intCast(args[0])] orelse return 0;
    if (psx.getBits(p, "unk5AC_11") != 0) return 1;
    if (psx.getBits(p, "unk5AC_9") != 0) return 2;
    return 3;
}
