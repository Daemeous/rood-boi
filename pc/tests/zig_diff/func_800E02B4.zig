//! zig_diff spec: func_800E02B4 -> func_800E02B4.func_800E02B4 (771A8.c).
//! Inputs: the scratchpad obstruction cache (tested bits random, so both the
//! early return and the body run), the direction table at 0x1F8003EC (dx and
//! dz as 2-bit signed fields, every value), directions 0-15 (8-15 index past
//! the 8-entry table and cache), ahead 0, small negatives and positives, and
//! +-1000. The state in the arena has a random id 0-3, a position near zero,
//! step 0/1/2/255, radius with the 65535 edge, and a signed height (negative
//! heights make the unsigned height bound huge). The GTE-free points stub aims
//! each of its four points at the probe point with offsets on, just inside
//! and just outside the bounds, or far away, and returns a count of 0-4. Up to
//! four actors sit in the vs_battle_actors list (or none), each with an id
//! 0-15 whose scratchpad slot points at an other state: id 0 (skipped), the
//! state's own id (skipped), or another id; its model is aimed at the probe.

const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_771A8");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800E02B4));

pub const classes = [_][:0]const u8{ "cached", "no-actors", "ahead-zero", "moving" };

// The probe point and bounds of the current run: prepare sets them, the
// points stub aims its points with them. Both runs see the same values.
var tx: i32 = 0;
var ty: i32 = 0;
var tz: i32 = 0;
var rad: i32 = 0;
var hgt: i32 = 0;

const cache_addr: usize = 0x1F80_03D0;
const dirs_addr: usize = 0x1F80_03EC;
const slots_addr: usize = 0x1F80_037C;

fn cache() *v.obstructionCache {
    return @ptrFromInt(cache_addr);
}

/// A coordinate relative to t: on the bound b, one past it, its negation, or
/// (sel 8..15) the extra value, usually far outside every bound.
fn aimed(t: i32, b: i32, sel: u8, extra: i32) i16 {
    const off: i32 = switch (sel % 16) {
        0, 1 => 0,
        2 => b,
        3 => b + 1,
        4 => -b,
        5 => -b - 1,
        6 => b - 1,
        7 => 1 - b,
        else => extra,
    };
    return @truncate(t + off);
}

pub const stubs = struct {
    /// The obstruction points: four points aimed at the probe, and a count 0-4.
    pub fn func_8008D2C0(ctx: *zd.Ctx, p: anytype) c_int {
        const pts: [*]v.obstructionVector = @ptrCast(@alignCast(p));
        var k: usize = 0;
        while (k < 4) : (k += 1) {
            pts[k].x = aimed(tx, rad, ctx.value(u8), @as(i32, ctx.value(i16)));
            pts[k].y = aimed(ty, hgt, ctx.value(u8), @as(i32, ctx.value(i16)));
            pts[k].z = aimed(tz, rad, ctx.value(u8), @as(i32, ctx.value(i16)));
            pts[k].pad = ctx.value(i16);
        }
        return @intCast(ctx.value(u8) % 5);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    // The direction table: dx and dz of each of the first eight directions.
    var d: usize = 0;
    while (d < 8) : (d += 1) {
        const e: *v.D_800F16EC_t = @ptrFromInt(dirs_addr + d);
        psx.setBits(e, "dx", @bitCast(ctx.int(u2)));
        psx.setBits(e, "dz", @bitCast(ctx.int(u2)));
    }

    const direction: c_int = ctx.range(c_int, 0, 15);
    const ahead: c_int = if (ctx.chance(3))
        0
    else if (ctx.chance(2))
        ctx.range(c_int, -3, 3)
    else
        ctx.range(c_int, -1000, 1000);

    // The cache: the bit for this direction is set half the time (early return).
    const c = cache();
    const bit: u8 = if (direction < 8) @as(u8, 1) << @intCast(direction) else 0;
    if (ctx.chance(2)) c.tested |= bit else c.tested &= ~bit;

    const st = ctx.alloc(v.obstructionState);
    st.position.x = if (ctx.chance(10)) ctx.int(i16) else ctx.range(i16, -2000, 2000);
    st.position.y = if (ctx.chance(10)) ctx.int(i16) else ctx.range(i16, -2000, 2000);
    st.position.z = if (ctx.chance(10)) ctx.int(i16) else ctx.range(i16, -2000, 2000);
    st.step = ctx.pick(u8, &.{ 0, 1, 2, 255 });
    st.radius = ctx.pick(u16, &.{ 0, 1, 2, 40, 200, 65535 });
    st.height = ctx.pick(i16, &.{ -100, -33, -32, -1, 0, 1, 31, 100 });
    st.id = ctx.range(u8, 0, 3);

    // The probe point: x and z move by ahead steps along the direction.
    const e: *v.D_800F16EC_t = @ptrFromInt(dirs_addr + @as(usize, @intCast(direction)));
    const dxv: i32 = psx.getBits(e, "dx");
    const dzv: i32 = psx.getBits(e, "dz");
    tx = @as(i32, st.position.x) + ahead * (@as(i32, st.step) * dxv);
    ty = @as(i32, st.position.y);
    tz = @as(i32, st.position.z) + ahead * (@as(i32, st.step) * dzv);
    rad = @as(i32, st.radius) + 96;
    hgt = (@as(i32, st.height) + 32) >> 1;

    // The actor list: 0 to 4 actors, each with an other state in the id table.
    const n: usize = if (ctx.chance(6)) 0 else ctx.range(usize, 1, 4);
    var actors = [_]?*v.vs_battle_actor{ null, null, null, null };
    var k: usize = 0;
    while (k < n) : (k += 1) {
        const a = ctx.alloc(v.vs_battle_actor);
        a.id = ctx.range(i32, 0, 15);
        actors[k] = a;

        const other = ctx.alloc(v.obstructionState);
        other.id = if (ctx.chance(5)) 0 else if (ctx.chance(3)) st.id else ctx.range(u8, 1, 3);
        other.radius = ctx.pick(u16, &.{ 0, 1, 2, 40, 200 });
        other.step = ctx.pick(u8, &.{ 0, 1, 2, 255 });
        other.height = ctx.pick(i16, &.{ -100, -33, -1, 0, 1, 31, 100 });

        const model = ctx.alloc(v.obstructionModel);
        psx.put(&other.model, model);
        const rb: i32 = @as(i32, other.radius) + @as(i32, st.radius) + @as(i32, other.step);
        const hb: i32 = (@as(i32, st.height) + @as(i32, other.height)) >> 1;
        model.position.x = aimed(tx, rb, ctx.int(u8), ctx.range(i32, -32768, 32767));
        model.position.y = aimed(ty, hb, ctx.int(u8), ctx.range(i32, -32768, 32767));
        model.position.z = aimed(tz, rb, ctx.int(u8), ctx.range(i32, -32768, 32767));

        const slot: *u32 = @ptrFromInt(slots_addr + 4 * @as(usize, @intCast(a.id)));
        slot.* = psx.addr(other);
    }

    k = 0;
    while (k < n) : (k += 1) {
        const nxt: ?*v.vs_battle_actor = if (k + 1 < n) actors[k + 1] else null;
        psx.put(&actors[k].?.next, nxt);
    }
    v.g.vs_battle_actors()[0] = if (n > 0) actors[0] else null;

    return .{ st, direction, ahead };
}

pub fn classify(args: Args) usize {
    if (((@as(u32, cache().tested) >> @intCast(args[1])) & 1) != 0) return 0;
    if (v.g.vs_battle_actors()[0] == null) return 1;
    if (args[2] == 0) return 2;
    return 3;
}
