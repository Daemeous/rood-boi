//! Target movement: advances a targeting actor's movement toward its target and returns 0 when the target cannot be reached (pc/src/game/BATTLE/BATTLE.PRG/771A8.c).
const psx = @import("common").psx;
const pad = @import("common").pad;
const v = @import("views_771A8");
const c = @import("c_771A8");

/// orig: vs_battle_aiUpdateTargetApproachMovement (pc/src/game/BATTLE/BATTLE.PRG/771A8.c)
/// Runs one frame of targeting movement for state `s` toward its target and returns 1 while the target is reachable.
pub fn vs_battle_aiUpdateTargetApproachMovement(s: ?*v.vs_battle_aiState, option: c_int) callconv(.c) c_int {
    const st = s.?;
    var result: c_int = 1;
    var opt: c_int = option;
    const target: c_int = st.actorId;
    done: {
        if (target == @as(c_int, st.id)) {
            c.vs_battle_aiIssueActorCommand(@ptrCast(st), 0);
            break :done;
        }
        const model: *align(1) v.targetMovementModel = @ptrCast(psx.at(v.g.vs_actors_actorRecords(), target).*.?);
        if ((psx.get(st.object.targetMovementModel).?.flags & 0x200000) != 0) opt = 1;
        const moved: bool = if ((model.flags & 0x1F0000) == 0)
            ((@as(c_int, model.x) >> 6) != @as(c_int, st.targetTileX) or (@as(c_int, model.z) >> 6) != @as(c_int, st.targetTileZ))
        else
            (@as(c_int, st.destination168.targetMovementPoint.b[0]) != (@as(c_int, st.targetTileX) >> 1) or @as(c_int, st.destination168.targetMovementPoint.b[2]) != (@as(c_int, st.targetTileZ) >> 1));
        if (moved or st.range != st.previousRange or target != @as(c_int, st.previousTargetId) or opt != @as(c_int, st.previousOption) or (blk: {
            const old = st.timerF8;
            st.timerF8 +%= 1;
            break :blk @as(c_int, old);
        }) > 180) {
            st.timerF8 = 0;
            st.bE8 = 0;
            st.bE7 = 0;
        }
        st.previousOption = psx.conv(u8, opt);
        st.previousTargetId = psx.conv(u8, target);
        st.previousRange = st.range;
        const tgt = psx.at(&st.tbl.t.targets.movement, target);
        if (psx.getBits(tgt, "f0") != 0) {
            st.bE4 = 0;
            if (psx.getBits(tgt, "f2") != 0) {
                const mode: c_int = st.rangeMode;
                const dist: c_uint = psx.at(&st.tbl.t.distances, target).*;
                switch (mode) {
                    0 => {
                        const d: c_int = st.range -% 96;
                        st.rangeMode = if (psx.conv(c_uint, d *% d) < dist) 0 else 1;
                    },
                    1 => {
                        const value: c_int = st.range;
                        if (psx.conv(c_uint, value *% value) < dist) {
                            st.rangeMode = 0;
                        } else {
                            const e: c_int = value -% 128;
                            if (psx.conv(c_uint, e *% e) >= dist) {
                                st.rangeMode = 2;
                            } else {
                                st.rangeMode = psx.conv(u8, mode);
                            }
                        }
                    },
                    2 => {
                        const d: c_int = st.range -% 32;
                        if (psx.conv(c_uint, d *% d) < dist) {
                            st.rangeMode = 1;
                        } else {
                            st.rangeMode = psx.conv(u8, mode);
                        }
                    },
                    else => {},
                }
            } else {
                st.rangeMode = 0;
            }
            const forced = (st.rangeMode != 2 and st.bE8 != 0) or (st.rangeMode != 0 and st.bE7 != 0);
            if (!forced and opt == 0 and st.rangeMode == 2) st.rangeMode = 1;
            const sel: c_int = if (forced) 1 else st.rangeMode;
            switch (sel) {
                0 => {
                    if (opt == 1) st.flags0.b[0] = 1;
                    st.flagsC.b[0] = 1;
                    st.flags0.b[1] = psx.conv(u8, c.func_800E4FB0(st, target, st.range));
                    if ((st.flags & 8) != 0) {
                        const oldPosition: c_uint = st.destination168.targetMovementPoint.raw;
                        const dx: c_int = @as(c_int, st.position.v.vx) -% (@as(c_int, st.destination168.targetMovementPoint.b[0]) << 7) -% 64;
                        const dz: c_int = @as(c_int, st.position.v.vz) -% (@as(c_int, st.destination168.targetMovementPoint.b[2]) << 7) -% 64;
                        const angle: c_int = c.ratan2(dx, dz) & 4095;
                        var distance: c_int = psx.conv(c_int, c.vs_core_mathSquaredVectorLength(dx, 0, dz));
                        st.previous48.targetMovementPoint.raw = oldPosition;
                        distance = c.vs_gte_rsqrt(distance);
                        const radius: c_int = st.range -% 96;
                        const absRadius: c_int = if (radius >= 0) radius else -%radius;
                        c.vs_battle_aiProjectClearRadialDestination(@ptrCast(st), st.tile.position.raw, angle, psx.conv(c_uint, distance -% absRadius));
                        if ((st.destination168.targetMovementPoint.raw & 0xFF00FF) == (st.tile.position.raw & 0xFF00FF)) {
                            st.flagsC.b[1] = 1;
                            st.destination168.targetMovementPoint.raw = oldPosition;
                        }
                    }
                    st.movementTargetId = st.actorId;
                    c.vs_battle_aiDispatchAiLocomotionMode(st, 0, 0);
                },
                1 => {
                    st.movementTargetId = psx.conv(u8, target);
                    c.vs_battle_aiRecoverMovement(@ptrCast(st));
                },
                2 => {
                    st.flags0.b[3] = 1;
                    const spad: *u8 = pad.at(u8, 0x3D0);
                    spad.* = 0;
                    st.flags4.b[0] = 1;
                    st.moveX = model.x;
                    st.moveZ = model.z;
                    if (st.f33 != 0) {
                        st.flags0.b[1] = 1;
                        st.flags8.b[1] = 1;
                        st.targetX = psx.conv(i16, st.moveX);
                        st.targetZ = psx.conv(i16, st.moveZ);
                    } else {
                        st.movementTargetId = psx.conv(u8, target);
                    }
                    const angle: c_int = c.ratan2(st.moveX -% @as(c_int, st.position.v.vx), st.moveZ -% @as(c_int, st.position.v.vz));
                    c.vs_battle_aiDispatchAiLocomotionMode(st, 1, angle);
                    if (psx.get(st.actor).?.id >= 9) st.bE8 = 1;
                },
                else => {},
            }
        } else {
            if (st.bE4 == 0 and psx.getBits(tgt, "f1") != 0) {
                if (st.f24 != 0) {
                    st.bE4 = 1;
                    st.timeout = 255;
                    st.cachedDestination.b[0] = psx.conv(u8, @as(c_int, st.targetTileX) >> 1);
                    st.cachedDestination.b[2] = psx.conv(u8, @as(c_int, st.targetTileZ) >> 1);
                } else {
                    if (psx.getBits(tgt, "f5") != 0) {
                        result = 0;
                        break :done;
                    }
                    c.vs_battle_aiSetTemporaryAwarenessIndicator(@ptrCast(st), 7, 40);
                    if (c.func_800E4180(st, 128, 1, 1) == 0) result = 0;
                    break :done;
                }
            }
            if (st.bE8 != 0) {
                st.movementTargetId = psx.conv(u8, target);
                c.vs_battle_aiRecoverMovement(@ptrCast(st));
                break :done;
            }
            if (st.bE4 != 0) {
                st.destination168.targetMovementPoint.raw = st.cachedDestination.raw;
                if (st.timeout != 0) st.timeout -%= 1;
                if ((st.destination168.targetMovementPoint.raw & 0xFF00FF) == (st.tile.position.raw & 0xFF00FF)) {
                    if (psx.getBits(tgt, "f5") != 0) {
                        result = 0;
                        break :done;
                    }
                    c.vs_battle_aiSetTemporaryAwarenessIndicator(@ptrCast(st), 7, 40);
                    if (c.func_800E4180(st, 128, 1, 1) == 0) {
                        st.bE4 = 0;
                        result = 0;
                    }
                } else if (st.timeout != 0) {
                    st.flags0.b[1] = 1;
                    st.flagsC.b[0] = 1;
                    c.vs_battle_aiDispatchAiLocomotionMode(st, 0, 0);
                } else {
                    st.bE4 = 0;
                    result = 0;
                }
            } else if (c.func_800E4180(st, 0, 3, 1) == 0) {
                result = 0;
            }
        }
    }
    c.vs_battle_aiRefreshAiTargetTile(st);
    return result;
}
