//! zig_diff spec: func_800A70DC -> func_800A70DC.func_800A70DC (3A1A0.c).
//! Inputs: a D_800F4538_t in the arena with its 0x600 mask bits set, cleared or
//! random, unkA_3 = 2 sometimes, unkA_0 over 0/1/3/5, unk34.vy and position.vy
//! around zero and the i16 edges, R = unk63C over its edges, and unk4 chosen so
//! the sector lands in 0..7 or out of it (negative and above 7). The attribute
//! word behind D_800F49F0 has bit 5 set or clear at random. The height lookups
//! return the actor's height plus an offset picked per sample (equal, -192,
//! -193, -255, -256, -220, -64, -96, -32, or a random value). Half the inputs
//! lay the offsets out so the two do-while loops run to the final store, the
//! rest leave them early. The tile lookup returns NULL sometimes, so the C reads
//! PS1 address 0.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_3A1A0");
const c = @import("c_3A1A0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A70DC));

pub const classes = [_][:0]const u8{ "masked", "flat", "no-ramp", "sector-out", "ramp-out", "ramp-complete" };

/// Set by prepare: the actor's height (the stubs return offsets from it).
var cur_y: i16 = 0;
/// Set by the tile stub: the calls before the first height lookup.
var base: u32 = 0;
/// Set by prepare: the offset class of each height index (9 = a random value).
var cls: [8]u8 = .{ 0, 0, 0, 0, 0, 0, 0, 0 };
/// Set by prepare: the attribute word *D_800F49F0 points at.
var attr_word: ?*u32 = null;

/// The height lookups happen in this order: heights[0], [6], [2], [7], [1], [3], [5], [4].
const order = [8]usize{ 0, 6, 2, 7, 1, 3, 5, 4 };
/// Offsets from the actor's height for classes 0..8.
const offs = [9]c_int{ 0, -192, -193, -255, -256, -220, -64, -96, -32 };

fn sectorOf(raw: u16) i32 {
    const s: i32 = @as(i16, @bitCast(raw));
    return @divTrunc(s + 256, 512);
}

/// True when the offset classes walk both do-while loops to the final store.
fn rampComplete(sec: i32) bool {
    for (0..8) |h| {
        const d = @mod(@as(i32, @intCast(h)) - sec, 8);
        const k = cls[h];
        if (d == 0 or d == 1 or d == 7) {
            if (k != 2 and k != 3 and k != 5) return false;
        } else {
            if (k != 0 and k != 1 and k != 6 and k != 7 and k != 8) return false;
        }
    }
    return true;
}

pub const stubs = struct {
    pub fn func_800A8D64(ctx: *zd.Ctx, p: anytype, k: anytype) [*c]c.u_int {
        _ = p;
        _ = k;
        base = ctx.calls;
        if (ctx.value(u8) < 32) return null;
        const w = ctx.alloc(u32);
        w.* = ctx.value(u32);
        return @ptrCast(w);
    }

    pub fn func_800A6EE8(ctx: *zd.Ctx, p: anytype, dx: anytype, dz: anytype, f: anytype) c_int {
        _ = p;
        _ = dx;
        _ = dz;
        _ = f;
        if (attr_word) |w| {
            if (ctx.value(u8) < 16) w.* = ctx.value(u32);
        }
        const slot: usize = @as(usize, ctx.calls -% base -% 1) & 7;
        const k = cls[order[slot]];
        if (k == 9) return ctx.value(i16);
        return @as(c_int, cur_y) + offs[k];
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const actor = ctx.alloc(v.D_800F4538_t);

    // The attribute word behind D_800F49F0: bit 5 is the attribute.
    const attr = ctx.alloc(u32);
    attr.* = ctx.int(u32);
    if (ctx.chance(2)) {
        attr.* |= 0x20;
    } else {
        attr.* &= ~@as(u32, 0x20);
    }
    attr_word = attr;
    v.g.D_800F49F0().* = @ptrCast(attr);

    actor.unk1848.unk8 = if (ctx.chance(2)) 0 else ctx.int(u32);
    actor.unk63C = ctx.pick(u16, &.{ 0, 1, 64, 96, 0x7FFF, 0x8000, 0xFFFF });
    actor.unk0.position.vy = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768, 100, -100 });
    cur_y = actor.unk0.position.vy;
    actor.unk0.unk34.vy = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 });
    psx.setBits(&actor.unk0, "unkA_0", ctx.pick(u3, &.{ 0, 3, 3, 1, 5 }));
    if (ctx.chance(3)) {
        psx.setBits(&actor.unk0, "unkA_3", 2);
    } else {
        psx.setBits(&actor.unk0, "unkA_3", ctx.int(u2));
    }

    // The 0x600 mask: clear, one bit, or whatever the fill left.
    if (ctx.chance(3)) {
        psx.setBits(actor, "unk5AC_9", 0);
        psx.setBits(actor, "unk5AC_10", 0);
    } else if (ctx.chance(2)) {
        psx.setBits(actor, "unk5AC_9", 1);
        psx.setBits(actor, "unk5AC_10", ctx.int(u1));
    }

    // The sector: from unk4 inside 0..7, or an edge value out of it.
    if (ctx.chance(8)) {
        actor.unk1848.unk4 = ctx.pick(u16, &.{ 0x8000, 0xFE00, 0xFFFF, 0xFF00, 0x7FFF, 0x1000 });
    } else {
        const want = ctx.range(i32, 0, 7);
        const s: i32 = want * 512 - 256 + ctx.range(i32, 0, 511);
        actor.unk1848.unk4 = @bitCast(@as(i16, @intCast(s)));
    }
    const sec = sectorOf(actor.unk1848.unk4);

    // The offset classes: half walk the loops to the store, half are random.
    if (ctx.chance(2)) {
        for (0..8) |h| {
            const d = @mod(@as(i32, @intCast(h)) - sec, 8);
            if (d == 0 or d == 1 or d == 7) {
                cls[h] = ([_]u8{ 2, 3, 5 })[ctx.range(usize, 0, 2)];
            } else {
                cls[h] = ([_]u8{ 0, 1, 6, 7, 8, 9 })[ctx.range(usize, 0, 5)];
            }
        }
    } else {
        for (&cls) |*k| k.* = ctx.range(u8, 0, 9);
    }

    var diagonal = ctx.pick(c_int, &.{ 0, 1, -1, 64, -64, 0x7FFF_FFFF, -0x7FFF_FFFF });
    if (diagonal == std.math.minInt(c_int)) diagonal = 1;
    return .{ actor, diagonal };
}

pub fn classify(args: Args) usize {
    const actor = args[0].?;
    if ((psx.byteAt(u32, actor, 0x5AC).* & 0x600) != 0) return 0;
    if (psx.getBits(&actor.unk0, "unkA_3") == 2) return 1;
    if (psx.getBits(&actor.unk0, "unkA_0") == 0) return 2;
    const sec = sectorOf(actor.unk1848.unk4);
    if (sec < 0 or sec > 7) return 3;
    if (rampComplete(sec)) return 5;
    return 4;
}
