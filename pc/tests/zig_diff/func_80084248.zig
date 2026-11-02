//! zig_diff spec: func_80084248 -> func_80084248.func_80084248 (146C.c).
//! Inputs: an arena target entity naming an arena actor with a random
//! current HP; the status stub writes the entity's masks.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80084248));

pub const classes = [_][:0]const u8{ "any" };

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

pub const stubs = struct {
    pub fn _setInflictedStatuses(ctx: *zd.Ctx, id: anytype, ent: anytype) c_int {
        _ = id;
        const h: *v._hitEntity_t = @ptrCast(@alignCast(ent.?));
        h.inflictStatuses = ctx.value(i32);
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const t = targetWithStats(ctx);
    t[1].currentHP = ctx.pick(i16, &.{ 0, 1, -1, 0x7FFF, -0x8000 });
    return .{ ctx.alloc(v.vs_action_t), ctx.alloc(v._hitEntity_t), t[0], ctx.int(c_int), ctx.int(c_int) };
}

pub fn classify(_: Args) usize {
    return 0;
}
