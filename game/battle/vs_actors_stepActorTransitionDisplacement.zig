//! Spends one step of an actor's velocity: moves 1/unk5CA of its unk34 velocity into its position and counts unk5CA down (pc/src/game/BATTLE/BATTLE.PRG/3A1A0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_3A1A0");

/// orig: vs_actors_stepActorTransitionDisplacement (pc/src/game/BATTLE/BATTLE.PRG/3A1A0.c)
/// Moves 1/unk5CA of each velocity axis into the position and counts unk5CA down by one.
pub fn vs_actors_stepActorTransitionDisplacement(arg0: ?*v.D_800F4538_t) callconv(.c) void {
    const actor = arg0.?;

    const stepX: c_int = @divTrunc(@as(c_int, actor.unk0.unk34.vx), @as(c_int, actor.unk5CA));
    actor.unk0.unk34.vx = psx.conv(i16, @as(c_int, actor.unk0.unk34.vx) -% stepX);
    actor.unk0.position.vx = psx.conv(i16, @as(c_int, actor.unk0.position.vx) +% stepX);

    const stepY: c_int = @divTrunc(@as(c_int, actor.unk0.unk34.vy), @as(c_int, actor.unk5CA));
    actor.unk0.unk34.vy = psx.conv(i16, @as(c_int, actor.unk0.unk34.vy) -% stepY);
    actor.unk0.position.vy = psx.conv(i16, @as(c_int, actor.unk0.position.vy) +% stepY);

    const stepZ: c_int = @divTrunc(@as(c_int, actor.unk0.unk34.vz), @as(c_int, actor.unk5CA));
    actor.unk0.unk34.vz = psx.conv(i16, @as(c_int, actor.unk0.unk34.vz) -% stepZ);
    actor.unk0.position.vz = psx.conv(i16, @as(c_int, actor.unk0.position.vz) +% stepZ);

    actor.unk5CA -%= 1;
}
