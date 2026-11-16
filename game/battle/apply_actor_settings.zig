//! Fills an actor's settings state from its settings word and its actor's flags
//! (pc/src/game/BATTLE/BATTLE.PRG/7CD68.c).
const psx = @import("common").psx;
const v = @import("views_7CD68");
const c = @import("c_7CD68");

// The unit's own prototype names vs_battle_aiState, whose table is opaque in
// the c module; the call passes the same pointer, so the untyped pointer is the
// same ABI without pulling that declaration into this object.
extern fn vs_battle_aiBuildAiActionMetadata(state: ?*anyopaque) callconv(.c) void;

/// orig: vs_battle_aiApplyActorAiSettings (pc/src/game/BATTLE/BATTLE.PRG/7CD68.c)
/// Fills an actor's settings state from its settings word and its actor's flags.
pub fn applyActorSettings(state: ?*v.vs_battle_aiState, settings: ?*v.actorSettingsWord) callconv(.c) void {
    const s = state.?;
    const w = settings.?;

    var rangeSquared: c_uint = v.g.vs_battle_aiPerceptionRangesSquared()[@as(usize, (w.half[1] & 28) >> 2)];
    s.rangeSquared = rangeSquared;
    if (rangeSquared > 0x23FFFF) rangeSquared = 0x240000;
    s.nearDistance = rangeSquared;
    s.angle128 = v.g.vs_battle_aiViewAngles()[@as(usize, w.half[1] & 3)][0];
    const halfAngle: c_int = @divTrunc(@as(c_int, s.angle128), 2);
    s.angleLimit = @bitCast(c.rcos(@divTrunc(halfAngle *% 4096, 360)));
    s.unk25 = psx.conv(u8, (w.raw >> 15) & 1);
    s.f29 = psx.conv(u8, (w.raw >> 14) & 1);
    s.f24 = psx.conv(u8, (w.raw >> 13) & 1);
    s.f2D = 0;
    if (v.g.D_800F58C0().* != 0) {
        const table = v.g.D_800F58C8().*.?;
        var i: c_int = 0;
        while (i < 8) : (i += 1) {
            if (table[@intCast(i)] >> 15 != 0) break;
        }
        if (i != 8) s.f2D = psx.conv(u8, (w.raw >> 12) & 1);
    }
    s.radiusB2 = psx.conv(u8, ((w.raw >> 10) & 3) +% 1);
    s.fB0 = v.g.D_800F17C8()[@as(usize, (w.raw >> 8) & 3)][0];
    var step: c_uint = 128;
    if (s.collisionRadius > 128) step = 32;
    s.delay = psx.conv(u16, (step *% @as(c_uint, v.g.D_800F17D8()[@as(usize, (w.raw >> 2) & 3)])) >> 4);
    s.f23 = 1;
    s.f21 = w.byte[3] & 1;
    s.unk22 = psx.conv(u8, (w.raw >> 23) & 1);
    s.f2C = psx.conv(u8, (w.raw >> 21) & 3);
    s.unk2F = psx.conv(u8, (w.raw >> 28) & 3);
    s.f31 = psx.conv(u8, w.raw >> 30);
    s.flag20 = psx.conv(u8, (w.raw >> 26) & 1);
    s.movePeriod = v.g.vs_battle_aiWanderTimingPresets()[@as(usize, (w.raw >> 4) & 7)].first;
    s.restPeriod = v.g.vs_battle_aiWanderTimingPresets()[@as(usize, (w.raw >> 4) & 7)].second;
    s.rate = v.g.vs_battle_aiWanderTimingPresets()[@as(usize, (w.raw >> 4) & 7)].third;
    s.flag27 = psx.conv(u8, (w.raw >> 27) & 1);
    s.f30 = psx.conv(u8, (w.raw >> 25) & 1);
    if ((s.flags & 0xC0) == 0) s.f32 = v.g.D_80069C18()[@as(usize, s.f31)];
    s.f33 = w.byte[0] >> 7;

    const actor = psx.get(s.actor).?;
    const mode: c_int = @intCast((@as(u32, @bitCast(actor.unk34)) >> 12) & 3);
    s.allowFallback = psx.conv(u8, mode >> 1);
    s.special = psx.conv(u8, mode & 1);
    s.rate102 = v.g.D_80069C08()[@as(usize, (@as(u32, @bitCast(actor.unk34)) >> 14) & 1)];
    s.row = psx.conv(u8, (actor.flags30.raw >> 14) & 3);
    s.fC8 = actor.flags30.half[1] & 63;
    s.fCC = psx.conv(u8, (actor.flags30.raw >> 22) & 31);
    s.fCE = psx.conv(u8, actor.flags30.raw >> 27);
    if (psx.conv(c_uint, @as(c_int, actor.subType) -% 0xAC) < 2) {
        const handler: ?*anyopaque = @ptrCast(@constCast(&c.func_800E7F8C));
        psx.put(&s.callback, handler);
        s.f19 = 1;
        c.vs_battle_aiAllocateOrbitingBossState(@ptrCast(s));
    } else {
        const entry = v.g.vs_battle_aiDecisionCallbacks()[@as(usize, (actor.flags30.raw >> 4) & 31)];
        psx.put(&s.callback, @as(?*anyopaque, @constCast(entry)));
    }
    s.target16 = 16;
    c.func_800E5998();
    vs_battle_aiBuildAiActionMetadata(@ptrCast(s));
}
