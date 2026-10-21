//! zig_diff spec: func_800A2574 -> func_800A2574.func_800A2574 (38C1C.c).
//! The C has no branches of its own: it fetches D_800F4538[arg0], makes two
//! calls and stores into the fetched block. The inputs cover arg0 over the
//! table indices 0-16 (index 0 apart from the rest, so both the zero and
//! nonzero index paths are exercised), and arg1 over the short edges 0, +-1,
//! 32767 and -32768 plus random values, so both signs occur. The table entry
//! is a D_800F4538_t in the arena, the only object the C dereferences. The
//! table holds native C pointers, so the slot is written as a native pointer.
//! The calls are recording stubs, so a moved store or a changed argument
//! shows up in the call hashes and in the final state bytes; func_8009DF3C
//! sometimes puts another actor in the slot, so a port that fetches the
//! slot after the calls (the C fetches it before) stores into the wrong one.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_38C1C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A2574));

pub const classes = [_][:0]const u8{
    "index0-arg1-negative",
    "index0-arg1-positive",
    "index-nonzero-arg1-negative",
    "index-nonzero-arg1-positive",
};

/// The actor func_8009DF3C puts in the slot (null: it leaves the slot alone).
var replacement: ?*v.D_800F4538_t = null;

pub const stubs = struct {
    pub fn func_8009DF3C(ctx: *zd.Ctx, slot: c_int, kind: c_int) void {
        _ = ctx;
        _ = kind;
        if (replacement) |r| psx.globalMany(*v.D_800F4538_t, "D_800F4538")[@intCast(slot)] = r;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const actor = ctx.alloc(v.D_800F4538_t);
    const table = psx.globalMany(*v.D_800F4538_t, "D_800F4538");
    const idx = ctx.range(u5, 0, 16);
    table[idx] = actor;
    replacement = if (ctx.chance(2)) ctx.alloc(v.D_800F4538_t) else null;
    const arg1 = ctx.pick(c_short, &.{ 0, 1, -1, 32767, -32768 });
    return .{ @as(c_int, idx), arg1 };
}

pub fn classify(args: Args) usize {
    const zero_index = args[0] == 0;
    const negative = args[1] < 0;
    const base: usize = if (zero_index) 0 else 2;
    return base + if (negative) @as(usize, 0) else 1;
}
