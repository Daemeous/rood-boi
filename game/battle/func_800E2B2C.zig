//! Movement decision step: turns an actor to its target's heading
//! (src/BATTLE/BATTLE.PRG/771A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_771A8");
const c = @import("c_771A8");

/// orig: func_800E2B2C (src/BATTLE/BATTLE.PRG/771A8.c)
/// Steps a movement decision toward targetId: 1 while it acts, 0 when idle or failed.
pub fn func_800E2B2C(context: ?*v.movementDecisionContext, targetId: c_int) callconv(.c) c_int {
    const ctx = context.?;
    const actor = psx.get(ctx.actor);
    if (targetId == @as(c_int, ctx.actorId)) {
        c.func_800DEEFC(@ptrCast(ctx), 0);
        return 1;
    }
    if (ctx.delay == 0) return 0;
    if (ctx.reaction != 0) {
        if (c.func_800E4180(ctx, 128, 3, 6) != 0) return 1;
        ctx.pending = 0;
        return 0;
    }
    const angle = c.ratan2(@as(c_int, ctx.x) -% ctx.targetX, @as(c_int, ctx.z) -% ctx.targetZ);
    if ((ctx.destination & 0xFF00FF) != (ctx.previousDestination & 0xFF00FF) and
        ((psx.at(&ctx.targetFlags, targetId).* >> 5) & 1) != 0)
    {
        if (ctx.pending == 0) {
            ctx.pending = 1;
            psx.setBits(ctx, "angle", psx.conv(i16, angle));
            ctx.previousDestination = ctx.destination;
        }
    }
    if (ctx.pending == 0) return 0;
    ctx.timer = ctx.delay;
    const facing: c_int = @as(c_int, actor.?.unk0.facing) +%
        @as(c_int, psx.conv(i16, actor.?.unk0.unk14));
    if ((facing & 0xFFF) == (@as(c_int, psx.getBits(ctx, "angle")) & 0xFFF)) {
        if (((psx.at(&ctx.targetFlags, targetId).* >> 5) & 1) == 0) {
            if (c.func_800E4180(ctx, 128, 1, 6) != 0) return 1;
            ctx.pending = 0;
            return 0;
        }
        ctx.pending = 1;
        psx.setBits(ctx, "angle", psx.conv(i16, angle & 0xFFF));
        ctx.previousDestination = ctx.destination;
    }
    c.func_800DEEFC(@ptrCast(ctx), 4);
    return 1;
}
