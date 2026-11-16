//! Movement decision step: turns an actor to its target's heading
//! (pc/src/game/BATTLE/BATTLE.PRG/771A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_771A8");
const c = @import("c_771A8");

/// orig: vs_battle_aiUpdateTargetFacing (pc/src/game/BATTLE/BATTLE.PRG/771A8.c)
/// Steps a movement decision toward targetId: 1 while it acts, 0 when idle or failed.
pub fn vs_battle_aiUpdateTargetFacing(context: ?*v.vs_battle_aiState, targetId: c_int) callconv(.c) c_int {
    const ctx = context.?;
    const actor = psx.get(ctx.object.D_800F4538_t);
    if (targetId == @as(c_int, ctx.id)) {
        c.vs_battle_aiIssueActorCommand(@ptrCast(ctx), 0);
        return 1;
    }
    if (ctx.delay == 0) return 0;
    if (ctx.reaction != 0) {
        if (c.func_800E4180(ctx, 128, 3, 6) != 0) return 1;
        ctx.pending = 0;
        return 0;
    }
    const angle = c.ratan2(@as(c_int, ctx.position.v.vx) -% ctx.moveX, @as(c_int, ctx.position.v.vz) -% ctx.moveZ);
    if ((ctx.destination168.destination & 0xFF00FF) != (ctx.previousDestination & 0xFF00FF) and
        ((psx.at(&ctx.tbl.targetFlags, targetId).* >> 5) & 1) != 0)
    {
        if (ctx.pending == 0) {
            ctx.pending = 1;
            ctx.angle18A = psx.conv(i16, angle);
            ctx.previousDestination = @bitCast(ctx.destination168.destination);
        }
    }
    if (ctx.pending == 0) return 0;
    ctx.timer13A = ctx.delay;
    const facing: c_int = @as(c_int, actor.?.unk0.facing) +%
        @as(c_int, psx.conv(i16, actor.?.unk0.unk14));
    if ((facing & 0xFFF) == (@as(c_int, ctx.angle18A) & 0xFFF)) {
        if (((psx.at(&ctx.tbl.targetFlags, targetId).* >> 5) & 1) == 0) {
            if (c.func_800E4180(ctx, 128, 1, 6) != 0) return 1;
            ctx.pending = 0;
            return 0;
        }
        ctx.pending = 1;
        ctx.angle18A = psx.conv(i16, angle & 0xFFF);
        ctx.previousDestination = @bitCast(ctx.destination168.destination);
    }
    c.vs_battle_aiIssueActorCommand(@ptrCast(ctx), 4);
    return 1;
}
