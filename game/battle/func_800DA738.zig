//! The da4bc decision step: steers toward the target or falls back to the candidate search (pc/src/game/BATTLE/BATTLE.PRG/71618.c).
const common = @import("common");
const pad = @import("common").pad;
const psx = common.psx;
const v = @import("views_71618");
const c = @import("c_71618");

/// orig: func_800DA738 (pc/src/game/BATTLE/BATTLE.PRG/71618.c)
/// Steers the da4bc state toward its target, falling back to the candidate search when that fails.
pub fn func_800DA738(state: ?*v.vs_battle_aiState) callconv(.c) c_int {
    var point: v.da4bcPoint = undefined;
    const candidate: c_int = c.vs_battle_aiFindActorByRelationship(@ptrCast(state), @ptrCast(&point), 3);
    const s = state.?;
    const target: c_int = @as(c_int, s.target16);
    const valid = (psx.conv(c_uint, ~candidate) >> 31) != 0;
    var active: c_int = 0;
    if (@as(c_int, s.id) != target and target != 16) {
        // psx_slot_get(vs_battle_aiState*, 0x1F80037C, target): a guest pointer slot in the scratchpad.
        const slot0: u32 = @as(*align(1) const u32, @ptrFromInt(@as(usize, psx.conv(u32, @as(c_int, @bitCast(pad.guest(0x37C))) +% (4 *% target))))).*;
        if (slot0 != 0) {
            active = @as(c_int, @intFromBool((psx.conv(u8, psx.at(&s.tbl.t.targets.bits32, target).*) >> 6) == 2));
        }
    }
    const oldFlag: c_int = s.flagF;
    s.flagF = psx.conv(u8, active);
    blk: {
        if (valid) {
            if (active != 0) {
                if (((psx.at(&s.tbl.t.targets.bits32, target).* >> 5) & 1) != 0) {
                    const first: *align(1) v.da4bcModel = @ptrCast(psx.at(v.g.vs_actors_actorRecords(), target).*.?);
                    const tile = first.tile;
                    const angleA: c_int = c.ratan2(
                        (@as(c_int, point.b[0]) << 7) -% (@as(c_int, first.x) -% 64),
                        (@as(c_int, point.b[2]) << 7) -% (@as(c_int, first.z) -% 64),
                    );
                    const second: *align(1) v.da4bcModel = @ptrCast(psx.at(v.g.vs_actors_actorRecords(), target).*.?);
                    const angleB: c_int = c.ratan2(
                        @as(c_int, s.position.v.vx) -% @as(c_int, second.x),
                        @as(c_int, s.position.v.vz) -% @as(c_int, second.z),
                    );
                    const cosA = c.rcos(angleA);
                    const cosB = c.rcos(angleB);
                    const sinA = c.rsin(angleA);
                    var dot: c_int = cosA *% cosB +% sinA *% c.rsin(angleB);
                    dot >>= 12;
                    if (s.timer44 != 0) s.timer44 -%= 1;
                    if (dot >= -1200 or s.timer44 != 0) {
                        // The tile word is passed by value: its bits are the C union's bits.
                        const tileBits: c.vs_battle_movementPosition = @bitCast(tile.raw);
                        c.vs_battle_aiProjectClearRadialDestination(@ptrCast(s), tileBits, c.ratan2(
                            @as(c_int, point.b[0]) -% psx.conv(c_int, tile.raw & 255),
                            @as(c_int, point.b[2]) -% psx.conv(c_int, (tile.raw >> 16) & 255),
                        ), 320);
                        psx.byteAt(u8, s, 1).* = 1;
                        c.vs_battle_aiDispatchAiLocomotionMode(@ptrCast(s), 0, 0);
                        if (s.timer44 == 0) s.timer44 = 10;
                        return 1;
                    }
                }
            } else break :blk;
        }
        if (active != 0) {
            if (valid) {
                const slotB_raw: u32 = @as(*align(1) const u32, @ptrFromInt(@as(usize, psx.conv(u32, @as(c_int, @bitCast(pad.guest(0x37C))) +% (4 *% target))))).*;
                const slotB: *align(1) v.vs_battle_aiState = @ptrFromInt(@as(usize, slotB_raw));
                if (psx.at(&slotB.tbl.t.distances, candidate).* <= 0x23FFF) {
                    s.movementTargetId = psx.conv(u8, target);
                    c.vs_battle_aiRecoverMovement(@ptrCast(s));
                    return 1;
                }
            }
            if (c.vs_battle_aiApproachActor(@ptrCast(s), target, 320, 1) != 0) return 1;
            if (c.vs_battle_aiTrackActorPosition(@ptrCast(s), target) != 0) return 1;
        }
    }
    // fallback:
    if (valid) {
        if (oldFlag != 0) {
            if (psx.conv(c_uint, c.vs_battle_aiNextActorRandomMasked(128)) < 128) s.unk18 = 1;
            c.vs_battle_aiSetTemporaryAwarenessIndicator(@ptrCast(s), 6, 0);
        }
        if (c.vs_battle_aiFleeVisibleEnemies(@ptrCast(s), 1) != 0) return 1;
    }
    return c.vs_battle_aiFleeVisibleEnemies(@ptrCast(s), 1);
}
