//! zig_diff spec: func_800A190C -> func_800A190C.func_800A190C (38C1C.c).
//! The inputs cover every action code the C tests (0xFB, 0xFD, 0xFA, the 0x40
//! family, and values near them, negative ones too) with arg0 over the
//! 17-entry actor table (16 included). Actors are present or absent in each
//! table, so the 0xFB fallback, the 0xFD and 0xFA physical-address-0 reads and
//! the -1 returns are all reached. The unk65A * unk183C products are often
//! negative and not multiples of 4096, which separates signed division from a
//! shift. Positions sit at the i16 wrap edges. The external stubs return 0,
//! -1, 1 or any value, so both sides of each `< 0` test are equal and differ.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_38C1C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A190C));

pub const classes = [_][:0]const u8{
    "fb-actor",
    "fb-fallback",
    "fb-none",
    "fd-actor",
    "fd-empty",
    "fa-actor",
    "fa-empty",
    "mask-40",
    "other",
};

const pos_edges = [_]i16{ 0, 1, -1, 0x7FFF, -0x8000, 0x40, -0x40 };
const scale_edges = [_]i16{ 0, 1, -1, 4096, -4096, 0x7FFF, -0x8000 };

fn newActor(ctx: *zd.Ctx) *v.D_800F4538_t {
    const a = ctx.alloc(v.D_800F4538_t);
    a.unk0.position.vx = ctx.pick(i16, &pos_edges);
    a.unk0.position.vy = ctx.pick(i16, &pos_edges);
    a.unk0.position.vz = ctx.pick(i16, &pos_edges);
    a.unk646 = ctx.pick(u16, &.{ 0, 1, 0xC0, 0xFFFF, 0x8000 });
    if (ctx.chance(2)) {
        // Small scales: the product is often negative and below 4096 in magnitude.
        a.unk65A = ctx.range(i16, -64, 64);
        a.unk183C = ctx.range(i16, -64, 64);
    } else {
        a.unk65A = ctx.pick(i16, &scale_edges);
        a.unk183C = ctx.pick(i16, &scale_edges);
    }
    return a;
}

fn pickArg1(ctx: *zd.Ctx) c_int {
    return ctx.pick(c_int, &.{ 0xFB, 0xFD, 0xFA, 0x40, 0x4F, 0x140, 0xFB | 0x100, 0xFA ^ 0x100, 0xFC, -0x40, 0xF0 });
}

/// Both-signed edge results: 0, -1, 1, or the raw value (half of the time negative).
fn edgeInt(ctx: *zd.Ctx) c_int {
    const raw = ctx.value(u32);
    return switch (raw % 6) {
        0 => 0,
        1 => -1,
        2 => 1,
        else => @bitCast(raw),
    };
}

pub const stubs = struct {
    pub fn func_800A152C(ctx: *zd.Ctx, _: anytype, _: anytype, _: anytype) c_int {
        return edgeInt(ctx);
    }
    pub fn func_800A1C10(ctx: *zd.Ctx, _: anytype, _: anytype, _: anytype, _: anytype) c_int {
        return edgeInt(ctx);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg0: c_int = if (ctx.chance(12)) 16 else ctx.range(c_int, 0, 15);
    const arg1 = pickArg1(ctx);
    const idx: usize = @intCast(arg0);
    const table = v.g.D_800F4538();
    const table2 = v.g.D_800F45E0();

    // The 0xFB path reads D_800F45E0[arg0], which has 16 entries: with arg0 16
    // the D_800F4538 entry must be present so that read never happens.
    var actor: ?*v.D_800F4538_t = null;
    if (!ctx.chance(3) or (arg1 == 0xFB and arg0 == 16)) actor = newActor(ctx);
    table[idx] = actor;

    if (arg1 == 0xFB and actor == null) {
        table2[idx] = null;
        if (!ctx.chance(3)) {
            const e: *v.D_800F45E0_t = @ptrCast(newActor(ctx));
            table2[idx] = e;
        }
    }

    const out = ctx.alloc(v.SVECTOR);
    return .{ arg0, arg1, out, ctx.int(c_int) };
}

pub fn classify(args: Args) usize {
    const idx: usize = @intCast(args[0]);
    const arg1 = args[1];
    const has = v.g.D_800F4538()[idx] != null;
    if (arg1 == 0xFB) {
        if (has) return 0;
        if (v.g.D_800F45E0()[idx] != null) return 1;
        return 2;
    }
    if (arg1 == 0xFD) return if (has) 3 else 4;
    if (arg1 == 0xFA) return if (has) 5 else 6;
    if ((arg1 & 0xF0) == 0x40) return 7;
    return 8;
}
