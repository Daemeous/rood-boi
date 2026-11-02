//! zig_diff spec: func_80084DA8 -> func_80084DA8.func_80084DA8 (146C.c).
//! Inputs: D_800F19CC an arena block whose current hit's hpEffect is 0..3;
//! an arena target entity naming an arena actor whose shield id is 0 half
//! the time; PP values at the edges so the 16-bit sum wraps.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80084DA8));

pub const classes = [_][:0]const u8{ "other-effect", "no-shield", "shield" };

pub const ignore_return = "the C has no return statement: _actionEffectMediators is declared void and its one caller (func_80085390) ignores the result";

/// A target entity whose targetActor names an arena actor (slots 0..15:
/// the table's live range) with an arena stats block.
fn targetWithStats(ctx: *zd.Ctx) struct { *v._hitEntity_t, *v.vs_battle_actor2 } {
    const entity = ctx.alloc(v._hitEntity_t);
    const slot = ctx.range(u8, 0, 15);
    entity.unk0.targetActor = slot;
    const actor = ctx.alloc(v.vs_battle_actor);
    const stats = ctx.alloc(v.vs_battle_actor2);
    actor.stats.raw = psx.addr(stats);
    v.g.vs_battle_actors()[slot] = actor;
    return .{ entity, stats };
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const block = ctx.alloc(v.D_800F19CC_t);
    v.g.D_800F19CC().* = block;
    psx.setBits(&block.unk8.unk4, "hpEffect", ctx.range(u2, 0, 3));
    const t = targetWithStats(ctx);
    t[1].shield.base.id = if (ctx.chance(2)) 0 else ctx.pick(u16, &.{ 1, 0x8000, 0xFFFF });
    t[1].shield.currentPp = ctx.pick(u16, &.{ 0, 1, 0x7FFF, 0x8000, 0xFFFF });
    t[0].shieldPp = ctx.pick(i16, &.{ 0, 1, -1, 0x7FFF, -0x8000 });
    return .{ ctx.alloc(v.vs_action_t), ctx.alloc(v._hitEntity_t), t[0], ctx.int(c_int), ctx.int(c_int) };
}

pub fn classify(args: Args) usize {
    const effect = psx.getBits(&v.g.D_800F19CC().*.?.unk8.unk4, "hpEffect");
    if (effect != 1 and effect != 3) return 0;
    const e = args[2].?;
    const s = psx.get(v.g.vs_battle_actors()[e.unk0.targetActor].?.stats).?;
    return if (s.shield.base.id == 0) 1 else 2;
}
