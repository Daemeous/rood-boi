//! zig_diff spec: _doesAttackHit -> does_attack_hit.doesAttackHit (146C.c).
//! Inputs: an arena action whose hit (0 or 1) names one of the 8 hit
//! functions (the table is filled with this spec's function, which returns
//! the chance prepare chose: 0, 50, 99..101, 254, 255, -1 and large, and
//! records its arguments in the arena); the action a spell or not; the
//! target naming an arena actor (unk957 0x80 or not, statuses with the top
//! bit and 4 or not), unk40 0 or not, its threshold 255 or not; the source
//! the player's (actor 0, unk40 0) or not; D_800F1A00 0 or not;
//! vs_main_getRand's stub around the threshold.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").doesAttackHit));

pub const classes = [_][:0]const u8{ "immune-check", "hit-function", "sure" };

var chance: c_int = 0;
var rand: c_int = 0;
var seen: ?*[5]c_int = null;

/// Hit function `k` of the table: records which ran and its arguments.
fn hit(comptime k: c_int) HitFunction {
    return &struct {
        fn f(action: ?*v.vs_action_t, source: ?*v._hitEntity_t, target: ?*v._hitEntity_t, number: c_int, variance: c_int) callconv(.c) c_int {
            const s = seen.?;
            s[0] = @bitCast(psx.addr(action) ^ psx.addr(source));
            s[1] = @bitCast(psx.addr(target));
            s[2] = number;
            s[3] = variance;
            s[4] = k;
            return chance;
        }
    }.f;
}

const HitFunction = *const fn (?*v.vs_action_t, ?*v._hitEntity_t, ?*v._hitEntity_t, c_int, c_int) callconv(.c) c_int;

pub const stubs = struct {
    pub fn vs_main_getRand(_: *zd.Ctx, n: anytype) c_int {
        _ = n;
        return rand;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    seen = @ptrCast(@alignCast(ctx.allocBytes(20, 16)));
    const table = psx.globalMany(?HitFunction, "_hitFunctions");
    inline for (0..8) |k| table[k] = hit(k);
    chance = ctx.edge(c_int, &.{ 0, 50, 99, 100, 101, 254, 255, -1, 0x1000, 60 });
    for (0..16) |k| v.g.vs_battle_actors()[k] = null;
    const slot = ctx.range(u8, 0, 15);
    const actor = ctx.alloc(v.vs_battle_actor);
    const stats = ctx.alloc(v.vs_battle_actor2);
    stats.unk957 = if (ctx.chance(3)) 0x80 else ctx.int(u8);
    stats.statuses = ctx.pick(i32, &.{ 0, 4, -1, -0x8000_0000, 0x7FFF_FFFB });
    actor.stats.raw = psx.addr(stats);
    v.g.vs_battle_actors()[slot] = actor;
    const target = ctx.alloc(v._hitEntity_t);
    target.unk0.targetActor = slot;
    target.unk40 = if (ctx.chance(3)) ctx.pick(i32, &.{ 1, -1 }) else 0;
    target.unk0.hitThreshold = if (ctx.chance(2)) 255 else ctx.int(u8);
    const source = ctx.alloc(v._hitEntity_t);
    source.unk40 = if (ctx.chance(3)) ctx.pick(i32, &.{ 1, -1 }) else 0;
    source.unk0.targetActor = if (ctx.chance(2)) 0 else ctx.int(u8);
    const action = ctx.alloc(v.vs_action_t);
    v.g.D_800F1A00().* = if (ctx.chance(5)) 0 else ctx.pick(u32, &.{ 1, 0x8000_0000 });
    const bounded = if (chance > 100) @as(c_int, 100) else chance;
    rand = ctx.edge(c_int, &.{ 0, 99, bounded - 1, bounded, bounded + 9, bounded + 10, bounded * 2 - 1, bounded * 2 });
    return .{ action, source, target, ctx.edge(c_int, &.{ 0, 0, 1 }), ctx.int(c_int) };
}

pub fn classify(args: Args) usize {
    if (v.g.D_800F1A00().* == 0) return 2;
    const t = args[2].?;
    const stats = psx.get(v.g.vs_battle_actors()[t.unk0.targetActor].?.stats).?;
    return if (t.unk40 == 0 and stats.unk957 == 0x80) 0 else 1;
}
