//! zig_diff spec: func_8009E4B0 -> func_8009E4B0.func_8009E4B0 (30DB0.c).
//! Inputs: arg0 in 0..16. Index 16 is only used with a present actor, since the C
//! reads D_800F45E0 only when D_800F4538[arg0] is null and that table has 16 entries.
//! A present actor has unk9_0 from {0, 5, 11, 1, 15} (the "!= 0, != 5, != 11" tests),
//! unkC54.unk548_16 as 0 or -1, and animationId from an edge list around 28, 59,
//! 64-70, 85-94, 100, 109-113, 128, 164 and 198-202, or any byte. A null actor has
//! D_800F45E0[arg0] null or not. No external calls.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009E4B0));

pub const classes = [_][:0]const u8{
    "null-no-45e0",
    "null-with-45e0",
    "unk9-other",
    "unkC54-bit",
    "anim-under-100",
    "anim-100-plus",
};

const anim_edges = [_]u8{
    0,   28,  59,  63,  64,  70,  71,  84,  85,  91,  92,  94,  95,  99,  100, 101, 108,
    109, 110, 111, 112, 113, 127, 128, 129, 163, 164, 165, 197, 198, 199, 201, 202, 203, 255,
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg0 = ctx.range(c_int, 0, 16);
    const idx: usize = @intCast(arg0);
    const tbl = v.g.D_800F4538();
    const tbl45 = v.g.D_800F45E0();

    if (arg0 == 16 or !ctx.chance(3)) {
        const actor = ctx.alloc(v.D_800F4538_t);
        tbl[idx] = actor;
        psx.setBits(&actor.unk0, "unk9_0", ctx.edge(u4, &.{ 0, 5, 11, 1, 15 }));
        psx.setBits(&actor.unkC54, "unk548_16", ctx.edge(i1, &.{ 0, -1 }));
        actor.animationId = if (ctx.chance(4)) ctx.int(u8) else ctx.edge(u8, &anim_edges);
    } else {
        tbl[idx] = null;
        const other: ?*v.D_800F45E0_t = if (ctx.chance(2)) null else ctx.alloc(v.D_800F45E0_t);
        tbl45[idx] = other;
    }
    return .{arg0};
}

pub fn classify(args: Args) usize {
    const idx: usize = @intCast(args[0]);
    const tbl = v.g.D_800F4538();
    const t = tbl[idx] orelse {
        return if (v.g.D_800F45E0()[idx] == null) 0 else 1;
    };
    const b9 = psx.getBits(&t.unk0, "unk9_0");
    if (b9 != 0 and b9 != 5 and b9 != 11) return 2;
    if (psx.getBits(&t.unkC54, "unk548_16") != 0) return 3;
    return if (t.animationId >= 100) 5 else 4;
}
