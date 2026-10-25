//! zig_diff spec: func_8009EE9C -> func_8009EE9C.func_8009EE9C (30DB0.c).
//! Inputs: an index 0..16 into the D_800F4538 table (both ends and the middle),
//! a D_800F4538_t in the arena with the weaponDrawn, unkA_5, unkA_6 and unkA_0
//! (every 3-bit value, so 2 and 3 are covered) cases, unkF zero and nonzero,
//! unk63C around 0x80 with the high bit set, an optional SVECTOR (passed on,
//! never read), and any int for the unused arg2. The callees return -228, 172
//! and the values around them (the ordered comparisons' equal sides), and
//! write back the fields the C reads after the calls (unkF, unkA_0).
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009EE9C));

pub const classes = [_][:0]const u8{ "not-drawn", "busy-blocked", "entered-E180", "entered-no-E180", "unkF-zero" };

/// The object behind a callee's pointer argument (optional or not).
fn view(p: anytype) *v.D_800F4538_t {
    const q = if (@typeInfo(@TypeOf(p)) == .optional) p.? else p;
    const r: *v.D_800F4538_t = @ptrCast(@alignCast(q));
    return r;
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const t = ctx.alloc(v.D_800F4538_t);
    // The C reads D_800F4538[arg0] for arg0 in 0..16 only (17 entries).
    const idx: c_int = if (ctx.chance(2)) ctx.edge(c_int, &.{ 0, 16 }) else ctx.range(c_int, 0, 16);
    v.g.D_800F4538()[@intCast(idx)] = t;

    t.unk0.unkF = if (ctx.chance(2)) 0 else ctx.pick(u8, &.{ 1, 0x80, 0xFF });
    psx.setBits(&t.unk0, "weaponDrawn", if (ctx.chance(5)) 0 else 1);
    psx.setBits(&t.unk0, "unkA_5", @intFromBool(ctx.chance(3)));
    psx.setBits(&t.unk0, "unkA_6", @intFromBool(ctx.chance(3)));
    psx.setBits(&t.unk0, "unkA_0", ctx.edge(u3, &.{ 0, 1, 2, 3, 4, 5, 6, 7 }));
    t.unk63C = ctx.pick(u16, &.{ 0x7F, 0x80, 0x81, 0x8000, 0xFFFF });

    const arg1: ?*v.SVECTOR = if (ctx.chance(4)) null else ctx.alloc(v.SVECTOR);
    return .{ idx, arg1, ctx.int(c_int) };
}

pub fn classify(args: Args) usize {
    const t = v.g.D_800F4538()[@intCast(args[0])].?;
    if (psx.getBits(&t.unk0, "weaponDrawn") == 0) return 0;
    if (t.unk0.unkF == 0) return 4;
    if (psx.getBits(&t.unk0, "unkA_5") != 0 or psx.getBits(&t.unk0, "unkA_6") != 0) return 1;
    const a0 = psx.getBits(&t.unk0, "unkA_0");
    if (a0 == 2 or a0 == 3) return 1;
    return if (t.unk63C < 0x80) 2 else 3;
}

// func_800A0204 (the void call with arg0, var_a1, 0, var_s1) takes the default
// recording stub: it does nothing, and its arguments and the state are compared.
pub const stubs = struct {
    pub fn func_8009E180(ctx: *zd.Ctx, t: anytype, arg1: anytype) c_int {
        _ = arg1;
        // The C reads unkF again after the calls: this callee may change it.
        if (ctx.value(u1) == 1) {
            const d = view(t);
            d.unk0.unkF = ctx.value(u8);
        }
        return ctx.value(c_int);
    }

    pub fn func_8009E228(ctx: *zd.Ctx, t: anytype, arg1: anytype) c_int {
        _ = arg1;
        if (ctx.value(u2) == 0) {
            const d = view(t);
            d.unk0.unk11 = ctx.value(u8);
        }
        // The C tests this against -0xE4 and 0xAC: hit both equal sides often.
        return switch (ctx.value(u8) % 6) {
            0 => -228,
            1 => 172,
            2 => -229,
            3 => 171,
            4 => -228 + @as(c_int, @intCast(ctx.value(u32) % 401)),
            else => ctx.value(c_int),
        };
    }

    pub fn func_8009FD38(ctx: *zd.Ctx, t: anytype) void {
        const d = view(t);
        if (ctx.value(u2) == 0) psx.setBits(&d.unk0, "unkA_0", ctx.value(u3));
        if (ctx.value(u2) == 0) d.unk0.unkF = ctx.value(u8);
    }

    pub fn func_8006C84C(ctx: *zd.Ctx, arg: anytype) c_int {
        _ = arg;
        return ctx.value(c_int);
    }
};
