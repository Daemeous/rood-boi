//! zig_diff spec: func_8009D934 -> func_8009D934.func_8009D934 (30DB0.c).
//! Inputs: an arg0 slot in D_800F4538 (null or a random-state arena actor), arg2 from
//! the edges {0, 1, 2, 3, -1, -2, 1000, -1000} (arg0 stays in 0..9 when arg2 == 2, so
//! both wepModels slots stay inside the table; 0..16 otherwise), arg1 equal to the
//! actor's weaponDrawn bit about a third of the time and otherwise from edges that
//! include 0, 1, -1, 2, 256 and the int extremes. The actor's unk5B1 is 0..11 (the
//! tables have 12 entries), unk6E6 is zero or random. func_800BEB00 returns 4 about
//! 37% of the time, so the bail-out path is taken. func_800AE6C0 may change unk6E6.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009D934));

pub const classes = [_][:0]const u8{
    "no-actor",
    "wep-set",
    "unchanged",
    "holster",
    "draw",
    "draw-6E6-set",
};

fn view(p: anytype) *v.D_800F4538_t {
    const q = if (@typeInfo(@TypeOf(p)) == .optional) p.? else p;
    return @ptrCast(@alignCast(q));
}

pub const stubs = struct {
    // Writes unk6E6, a field the real callee may set; the C does not read it back.
    pub fn func_800AE6C0(ctx: *zd.Ctx, unit: anytype, a: anytype, b: anytype) void {
        _ = a;
        _ = b;
        if (ctx.value(u1) == 1) view(unit).unk6E6 = ctx.value(u16);
    }
    // Returns 4 (the C's bail-out test) in some calls, any other value otherwise.
    pub fn func_800BEB00(ctx: *zd.Ctx) c_short {
        if (ctx.value(u8) < 96) return 4;
        return ctx.value(c_short);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg2: c_int = ctx.edge(c_int, &.{ 0, 1, 2, 2, 3, -1, -2, 1000, -1000 });
    const arg0: c_int = @intCast(ctx.range(u32, 0, if (arg2 == 2) 9 else 16));

    const slots = v.g.D_800F4538();
    const t = ctx.alloc(v.D_800F4538_t);
    const wd = ctx.int(u1);
    psx.setBits(&t.unk0, "weaponDrawn", wd);
    t.unk5B1 = ctx.range(u8, 0, 11);
    t.unk6E6 = if (ctx.chance(3)) ctx.int(u16) else 0;

    const arg1: c_int = if (ctx.chance(3))
        @as(c_int, wd)
    else
        ctx.pick(c_int, &.{ 0, 1, -1, 2, 256, std.math.minInt(c_int), std.math.maxInt(c_int) });

    const no_actor = ctx.chance(6);
    slots[@intCast(arg0)] = if (no_actor) null else t;

    if (!no_actor and arg2 == 2) {
        const models = v.g.vs_battle_wepModels();
        for (0..2) |i| {
            const idx: usize = @intCast(arg0 * 2 + @as(c_int, @intCast(i)));
            models[idx] = if (ctx.chance(4)) null else ctx.alloc(v.vs_battle_wepModels_t);
        }
    }

    return .{ arg0, arg1, arg2 };
}

pub fn classify(args: Args) usize {
    const t = v.g.D_800F4538()[@intCast(args[0])] orelse return 0;
    if (args[2] == 2) return 1;
    if (args[1] == @as(c_int, psx.getBits(&t.unk0, "weaponDrawn"))) return 2;
    if (args[1] == 0) return 3;
    return if (t.unk6E6 == 0) 4 else 5;
}
