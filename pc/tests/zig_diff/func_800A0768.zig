//! zig_diff spec: func_800A0768 -> func_800A0768.func_800A0768 (30DB0.c).
//! Inputs: an index into the D_800F4538 table (0..16), whose entry is either
//! null or an arena D_800F4538_t, and a D_800F4538_unk58_2 source in the arena
//! (or null, which the C only reads when the entry is live). The source bytes
//! are random, so the three copied bytes cover every value, including the sign
//! bit. The function calls nothing, so there are no stubs.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A0768));

pub const classes = [_][:0]const u8{ "null-entry-null-arg", "null-entry-live-arg", "live-entry" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const idx: c_int = ctx.range(c_int, 0, 16);
    const table = v.g.D_800F4538();
    const src = ctx.alloc(v.D_800F4538_unk58_2);
    if (ctx.chance(3)) {
        // Null entry: the C returns -1 and never reads arg1.
        table[@intCast(idx)] = null;
        return .{ idx, if (ctx.chance(2)) null else src };
    }
    // Live entry: the C copies three bytes of arg1 into the object.
    table[@intCast(idx)] = ctx.alloc(v.D_800F4538_t);
    return .{ idx, src };
}

pub fn classify(args: Args) usize {
    const table = v.g.D_800F4538();
    if (table[@intCast(args[0])] == null) return if (args[1] == null) 0 else 1;
    return 2;
}
