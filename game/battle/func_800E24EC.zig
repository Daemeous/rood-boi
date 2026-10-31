//! Target movement: advances a targeting actor's movement toward its target and returns 0 when the target cannot be reached (src/BATTLE/BATTLE.PRG/771A8.c).
const psx = @import("common").psx;
const v = @import("views_771A8");
const c = @import("c_771A8");

/// orig: func_800E24EC (src/BATTLE/BATTLE.PRG/771A8.c)
/// Runs one frame of targeting movement for state `s` toward its target and returns 1 while the target is reachable.
pub fn func_800E24EC(s: ?*v.targetMovementState, option: c_int) callconv(.c) c_int {
    const st = s.?;
    var result: c_int = 1;
    var opt: c_int = option;
    const target: c_int = st.targetId;
    done: {
        if (target == @as(c_int, st.id)) {
            c.func_800DEEFC(@ptrCast(st), 0);
            break :done;
        }
        const model: *align(1) v.targetMovementModel = @ptrCast(psx.at(v.g.D_800F4538(), target).*.?);
        if ((psx.get(st.model).?.flags & 0x200000) != 0) opt = 1;
        const moved: bool = if ((model.flags & 0x1F0000) == 0)
            ((@as(c_int, model.x) >> 6) != @as(c_int, st.targetTileX) or (@as(c_int, model.z) >> 6) != @as(c_int, st.targetTileZ))
        else
            (@as(c_int, st.destination.b[0]) != (@as(c_int, st.targetTileX) >> 1) or @as(c_int, st.destination.b[2]) != (@as(c_int, st.targetTileZ) >> 1));
        if (moved or st.range != st.previousRange or target != @as(c_int, st.previousTargetId) or opt != @as(c_int, st.previousOption) or (blk: {
            const old = st.timer;
            st.timer +%= 1;
            break :blk @as(c_int, old);
        }) > 180) {
            st.timer = 0;
            st.bE8 = 0;
            st.bE7 = 0;
        }
        st.previousOption = psx.conv(u8, opt);
        st.previousTargetId = psx.conv(u8, target);
        st.previousRange = st.range;
        const tgt = psx.at(&st.targets, target);
        if (psx.getBits(tgt, "f0") != 0) {
            st.bE4 = 0;
            if (psx.getBits(tgt, "f2") != 0) {
                const mode: c_int = st.rangeMode;
                const dist: c_uint = psx.at(&st.distances, target).*;
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
                    if (opt == 1) st.b0 = 1;
                    st.bC = 1;
                    st.b1 = psx.conv(u8, c.func_800E4FB0(st, target, st.range));
                    if ((st.flags15E & 8) != 0) {
                        const oldPosition: c_uint = st.destination.raw;
                        const dx: c_int = @as(c_int, st.x) -% (@as(c_int, st.destination.b[0]) << 7) -% 64;
                        const dz: c_int = @as(c_int, st.z) -% (@as(c_int, st.destination.b[2]) << 7) -% 64;
                        const angle: c_int = c.ratan2(dx, dz) & 4095;
                        var distance: c_int = psx.conv(c_int, c.func_800E4660(dx, 0, dz));
                        st.previousDestination.raw = oldPosition;
                        distance = c.vs_gte_rsqrt(distance);
                        const radius: c_int = st.range -% 96;
                        const absRadius: c_int = if (radius >= 0) radius else -%radius;
                        c.func_800E1388(@ptrCast(st), st.tile.raw, angle, psx.conv(c_uint, distance -% absRadius));
                        if ((st.destination.raw & 0xFF00FF) == (st.tile.raw & 0xFF00FF)) {
                            st.bD = 1;
                            st.destination.raw = oldPosition;
                        }
                    }
                    st.movementTargetId = st.targetId;
                    c.func_800E50A0(st, 0, 0);
                },
                1 => {
                    st.movementTargetId = psx.conv(u8, target);
                    c.func_800E2CCC(@ptrCast(st));
                },
                2 => {
                    st.b3 = 1;
                    const spad: *u8 = @ptrFromInt(0x1F8003D0);
                    spad.* = 0;
                    st.b4 = 1;
                    st.targetX = model.x;
                    st.targetZ = model.z;
                    if (st.b33 != 0) {
                        st.b1 = 1;
                        st.b9 = 1;
                        st.destinationX = psx.conv(i16, st.targetX);
                        st.destinationZ = psx.conv(i16, st.targetZ);
                    } else {
                        st.movementTargetId = psx.conv(u8, target);
                    }
                    const angle: c_int = c.ratan2(st.targetX -% @as(c_int, st.x), st.targetZ -% @as(c_int, st.z));
                    c.func_800E50A0(st, 1, angle);
                    if (psx.get(st.actor).?.id >= 9) st.bE8 = 1;
                },
                else => {},
            }
        } else {
            if (st.bE4 == 0 and psx.getBits(tgt, "f1") != 0) {
                if (st.b24 != 0) {
                    st.bE4 = 1;
                    st.timeout = 255;
                    st.cachedDestination.b[0] = psx.conv(u8, @as(c_int, st.targetTileX) >> 1);
                    st.cachedDestination.b[2] = psx.conv(u8, @as(c_int, st.targetTileZ) >> 1);
                } else {
                    if (psx.getBits(tgt, "f5") != 0) {
                        result = 0;
                        break :done;
                    }
                    c.func_800D8260(@ptrCast(st), 7, 40);
                    if (c.func_800E4180(st, 128, 1, 1) == 0) result = 0;
                    break :done;
                }
            }
            if (st.bE8 != 0) {
                st.movementTargetId = psx.conv(u8, target);
                c.func_800E2CCC(@ptrCast(st));
                break :done;
            }
            if (st.bE4 != 0) {
                st.destination.raw = st.cachedDestination.raw;
                if (st.timeout != 0) st.timeout -%= 1;
                if ((st.destination.raw & 0xFF00FF) == (st.tile.raw & 0xFF00FF)) {
                    if (psx.getBits(tgt, "f5") != 0) {
                        result = 0;
                        break :done;
                    }
                    c.func_800D8260(@ptrCast(st), 7, 40);
                    if (c.func_800E4180(st, 128, 1, 1) == 0) {
                        st.bE4 = 0;
                        result = 0;
                    }
                } else if (st.timeout != 0) {
                    st.b1 = 1;
                    st.bC = 1;
                    c.func_800E50A0(st, 0, 0);
                } else {
                    st.bE4 = 0;
                    result = 0;
                }
            } else if (c.func_800E4180(st, 0, 3, 1) == 0) {
                result = 0;
            }
        }
    }
    c.func_800E4904(st);
    return result;
}
