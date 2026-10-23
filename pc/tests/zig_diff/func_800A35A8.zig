//! zig_diff spec: func_800A35A8 -> func_800A35A8.func_800A35A8 (3A1A0.c).
//! Inputs: three pool actors and three pool objects in the arena, shared by
//! the slots of the two global pointer tables (every table entry the C reads
//! is set). Actors cover null, flagged-out (unkA_5, unkA_0, unk5D = 2 and
//! above), and processed slots, with positions drawn from a small range so
//! object coordinates collide, plus full-range i16 values for wrapping.
//! The height stub returns a chosen value, or a random i16 whose low 15 bits
//! are the C's sign-extended height, so `obj->unk1E - 0x80 == h` happens
//! often and `<` sees equal sides. Objects are matched, unmatched, and null.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_3A1A0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A35A8));

pub const classes = [_][:0]const u8{ "none-processed", "no-object-match", "object-match" };

/// The height the stub returns when it returns the chosen value.
var h_want: i16 = 0;

pub const stubs = struct {
    pub fn func_8008DA24(ctx: *zd.Ctx, x: anytype, z: anytype) c_short {
        _ = x;
        _ = z;
        if (ctx.value(u1) == 1) return h_want;
        return ctx.value(c_short);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const actors = v.g.D_800F4538();
    const objs = v.g.D_800F45E0();

    h_want = if (ctx.chance(2)) ctx.int(i16) else ctx.pick(i16, &.{ 0, 1, -1, 127, 128, 16383, -16384, -32768, 32767 });
    // The C's `h <<= 17; h >>= 17;`: the low 15 bits, sign-extended.
    const h_eff: i32 = @as(i15, @truncate(h_want));

    var pa: [3]*v.D_800F4538_t = undefined;
    for (&pa) |*slot| {
        const a = ctx.alloc(v.D_800F4538_t);
        slot.* = a;
        psx.setBits(&a.unk0, "unkA_5", @intFromBool(ctx.chance(3)));
        psx.setBits(&a.unk0, "unkA_0", if (ctx.chance(2)) 0 else ctx.range(u3, 1, 7));
        a.unk0.unk5D = if (ctx.chance(4)) ctx.int(u8) else ctx.pick(u8, &.{ 0, 1, 2 });
        a.unk0.position.vx = if (ctx.chance(4)) ctx.int(i16) else ctx.range(i16, -2, 2);
        a.unk0.position.vz = if (ctx.chance(4)) ctx.int(i16) else ctx.range(i16, -2, 2);
    }

    var po: [3]*v.D_800F45E0_t = undefined;
    for (&po) |*slot| {
        const o = ctx.alloc(v.D_800F45E0_t);
        slot.* = o;
        const src = pa[ctx.range(usize, 0, 2)];
        if (ctx.chance(3)) {
            o.unk1C = src.unk0.position.vx;
            o.unk20 = src.unk0.position.vz;
        } else {
            o.unk1C = ctx.range(i16, -2, 2);
            o.unk20 = ctx.range(i16, -2, 2);
        }
        o.unk1E = if (ctx.chance(2)) @as(i16, @intCast(h_eff + 0x80)) else ctx.int(i16);
    }

    const skip_all = ctx.chance(10);
    var i: usize = 2;
    while (i < 17) : (i += 1) {
        actors[i] = if (skip_all or ctx.chance(6)) null else pa[ctx.range(usize, 0, 2)];
    }
    for (objs) |*slot| {
        slot.* = if (ctx.chance(3)) null else po[ctx.range(usize, 0, 2)];
    }

    return .{};
}

pub fn classify(_: Args) usize {
    const actors = v.g.D_800F4538();
    const objs = v.g.D_800F45E0();
    var processed = false;
    var matched = false;
    var i: usize = 2;
    while (i < 17) : (i += 1) {
        const a = actors[i] orelse continue;
        if (psx.getBits(&a.unk0, "unkA_5") != 0 or a.unk0.unk5D >= 2 or psx.getBits(&a.unk0, "unkA_0") != 0) continue;
        processed = true;
        for (objs) |oo| {
            const o = oo orelse continue;
            if (o.unk1C == a.unk0.position.vx and o.unk20 == a.unk0.position.vz) matched = true;
        }
    }
    if (!processed) return 0;
    return if (matched) 2 else 1;
}
