//! Moves one battle actor a step towards a heading: tilts, falls, stops and
//! animation choices from the actor's flags and the terrain probe
//! (src/BATTLE/BATTLE.PRG/3A1A0.c).
const psx = @import("common").psx;
const v = @import("views_3A1A0");
const c = @import("c_3A1A0");

/// The actor's status word: the u32 at byte 8.
inline fn statusWord(actor: *v.D_800F4538_t) c_uint {
    return psx.byteAt(c_uint, actor, 8).*;
}

/// The actor's movement flags word: the u32 at byte 0x5AC.
inline fn flagsWord(actor: *v.D_800F4538_t) c_uint {
    return psx.byteAt(c_uint, actor, 0x5AC).*;
}

/// The u32 at byte 0xC (the unkC_0 area nibble and its neighbours).
inline fn areaWord(actor: *v.D_800F4538_t) c_uint {
    return psx.byteAt(c_uint, actor, 0xC).*;
}

/// Label `fall` of func_800A5280: the actor starts to fall; always returns 3.
fn fall(actor: *v.D_800F4538_t) c_int {
    actor.unk0.unk34.vy = 3;
    actor.unk6E3 = 0xFF;
    psx.setBits(&actor.unk0, "unkA_0", 3);
    psx.setBits(&actor.unk0, "unkA_3", 0);
    psx.setBits(&actor.unk0, "unk9_6", 0);
    psx.setBits(&actor.unk0, "unkC_0", 0);
    psx.setBits(actor, "unk5AC_28", 0);
    return 3;
}

/// Label `done` of func_800A5280: queues the walk animation unless the actor is in a 0x70000 state.
fn done(actor: *v.D_800F4538_t, distance: c_int, direction: c_int, status: c_int) c_int {
    if ((statusWord(actor) & 0x70000) == 0) {
        _ = c.func_800A6AA0(@ptrCast(actor), distance, direction);
    }
    return status;
}

/// Label `reject` of func_800A5280: the step is refused; the actor keeps its old area.
fn reject(actor: *v.D_800F4538_t, turning: c_int, previousArea: c_int, direction: c_int) c_int {
    actor.unk0.unk34.vx = 0;
    actor.unk0.unk34.vz = 0;
    if (turning != 0) {
        actor.unk0.unk34.vy = 0;
        psx.setBits(&actor.unk0, "unkA_0", 3);
        return 3;
    }
    psx.setBits(&actor.unk0, "unkC_0", psx.getBits(&actor.unk0, "unkC_4"));
    psx.setBits(actor, "unk5AC_28", psx.conv(u4, previousArea));
    return done(actor, 0, direction, 3);
}

/// Label `blocked` of func_800A5280: refused at once in the 0x180000 states, else rejected.
fn blocked(actor: *v.D_800F4538_t, turning: c_int, previousArea: c_int, direction: c_int) c_int {
    if ((statusWord(actor) & 0x180000) != 0) {
        return 3;
    }
    return reject(actor, turning, previousArea, direction);
}

/// Label `fallStep` of func_800A5280: a falling actor's step (no sideways motion when the 0x1000 flag is set).
fn fallStep(index: c_int, actor: *v.D_800F4538_t, motion: *v.SVECTOR) void {
    if ((flagsWord(actor) & 0x1000) != 0) {
        motion.vx = 0;
        motion.vz = 0;
    }
    c.func_800AA600(index, psx.cptr(c.SVECTOR, motion), 0x18);
    actor.unk6EC = 0;
}

/// orig: func_800A5280 (src/BATTLE/BATTLE.PRG/3A1A0.c)
/// Moves actor `index` one step towards `direction` by `distance` (-1: by the stick vector in `input`),
/// handling falls, slopes, blocking terrain and the walk or fall animation; returns the probe status
/// (3 when the step is refused or handled elsewhere).
pub fn func_800A5280(index: c_int, direction_in: c_int, distance_in: c_int, input: ?*v.SVECTOR) callconv(.c) c_int {
    // Uninitialised in the original on some paths (the GCC object's value is not known here): the locals
    // `motion` (vy, pad), `facing` and `status` (returned by the `goto done` paths before func_800A76BC
    // ran) and `speed` (on the `goto stop` path) start at 0; `previousArea` below.
    var motion: v.SVECTOR = .{ .vx = 0, .vy = 0, .vz = 0, .pad = 0 };
    var saved: v.SVECTOR = .{ .vx = 0, .vy = 0, .vz = 0, .pad = 0 };
    var result: c_int = 0;
    var speed: c_int = 0;
    var facing: c_int = 0;
    var status: c_int = 0;
    var direction: c_int = direction_in;
    var distance: c_int = distance_in;
    var turning: c_int = 0;
    var animation: c_int = 0;
    var i: c_int = 0;

    if (index >= 17) {
        return 3;
    }
    const actor: *v.D_800F4538_t = psx.at(v.g.D_800F4538(), index).* orelse return 3;
    // Uninitialised in the original where the area is not read (statuses with bits 16-20): the unchanged
    // field makes reject's store a no-op.
    var previousArea: c_int = @as(c_int, psx.getBits(actor, "unk5AC_28"));
    if (psx.getBits(actor, "unk5AC_0") != 0) {
        return 3;
    }
    if ((statusWord(actor) & 0x0F000000) != 0) {
        return 3;
    }
    if ((flagsWord(actor) & 0x100000) != 0) {
        return 3;
    }
    if (@as(i8, @bitCast(actor.unk0.unk11)) > 0) {
        return 3;
    }
    if ((statusWord(actor) & 0x180000) == 0x100000) {
        if ((areaWord(actor) & 0xF) != 0) {
            c.func_800ACF54(@ptrCast(actor));
            if (psx.at(v.g.D_800F1D6E(), psx.getBits(&actor.unk0, "unkC_0")).* == 7 and
                c.func_800A6EE8(psx.cptr(c.SVECTOR, &actor.unk0.position), 0, 0, 1) <= @as(c_int, actor.unk0.position.vy))
            {
                return fall(actor);
            }
        }
        if (distance == 0) {
            return 3;
        }
        i = (direction -% @as(c_int, psx.conv(u16, actor.unk1800))) & 0xFFF;
        if (psx.conv(c_uint, i -% 0x400) <= 0x800) {
            return fall(actor);
        }
        if (actor.unk0.unk1A != 0) {
            return 3;
        }
        direction = @as(c_int, psx.conv(u16, actor.unk1800));
        if (distance >= @as(c_int, actor.unk5B9) << 12) {
            distance = @as(c_int, actor.unk5BA) << 12;
        }
    } else if ((statusWord(actor) & 0x180000) != 0) {
        i = (direction -% @as(c_int, psx.conv(u16, actor.unk1800))) & 0xFFF;
        if (distance == 0) {
            return 3;
        }
        if (i <= 0x400) {
            return 3;
        }
        if (i >= 0xC00) {
            return 3;
        }
        turning = 1;
        psx.setBits(&actor.unk0, "unkA_3", 0);
        psx.setBits(&actor.unk0, "unk9_6", 0);
        actor.unk0.unk1A = 0;
        actor.unk0.unk34.vx = 0;
        actor.unk0.unk34.vy = 0;
        actor.unk0.unk34.vz = 0;
        actor.unk6EC = 0;
        _ = c.func_800A0204(index, 1, 0, 10);
    }

    // Forward gotos of the C: `move`, `scale`, `coast`, `stop`, `halt`, each a labelled block whose
    // end is the label's code; `done`, `reject`, `blocked`, `fall` are the functions above.
    move: {
        scale: {
            coast: {
                stop: {
                    halt: {
                        if ((statusWord(actor) & 0x70000) != 0) {
                            if (index != 0) {
                                return 3;
                            }
                            const flags = psx.byteAt(v.vs_battleMovementModeFlags, actor, 0x5B0);
                            if (@divTrunc(distance, 4096) >= @as(c_int, actor.unk5B9)) {
                                if ((psx.byteAt(c_uint, actor, 0x5B0).* & 4) == 0) {
                                    distance = @as(c_int, actor.unk5BA) << 12;
                                }
                            } else {
                                psx.setBits(flags, "fast", 0);
                            }
                            const moveType: c_int = @as(c_int, psx.byteAt(u16, actor, 0xA).*) & 7;
                            if (moveType != 0) {
                                if (moveType < 4) {
                                    if (distance == 0) {
                                        break :stop;
                                    }
                                } else if (moveType == 4) {
                                    return 3;
                                }
                            }
                        }

                        if (distance == -1) {
                            if (input.?.vx == 0 and input.?.vz == 0) {
                                animation = actor.animationId;
                                if (animation >= 100) {
                                    animation -= 100;
                                }
                                if (animation == 1) {
                                    break :halt;
                                }
                                if (psx.conv(c_uint, animation -% 13) >= 14) {
                                    return 3;
                                }
                            }
                            motion.vx = input.?.vx;
                            motion.vz = input.?.vz;
                            speed = @as(c_int, actor.unk5BA) << 12;
                            break :move;
                        }
                        if (distance == 0) {
                            if ((flagsWord(actor) & 0x8000) != 0 and input.?.pad == 0) {
                                break :coast;
                            }
                            if (@as(c_int, actor.unk1840) >= @as(c_int, actor.unk5B9)) {
                                if (@as(c_int, actor.unk1846) >= 20 and @as(c_int, actor.unk1847) < 9) {
                                    actor.unk1846 = 0;
                                    psx.setBits(actor, "unk5AC_15", 1);
                                    if (input.?.pad == 0) {
                                        break :coast;
                                    }
                                    return done(actor, distance, direction, status);
                                }
                                actor.unk1846 = 0;
                            }
                            if (actor.unk6E4 == 0) {
                                if (actor.unk1840 != 0 or (areaWord(actor) & 0xF) != 0) {
                                    actor.unk1840 = 0;
                                } else if ((flagsWord(actor) & 0x1000) == 0) {
                                    animation = actor.animationId;
                                    if (animation >= 100) {
                                        animation -= 100;
                                    }
                                    if (animation != 0x54) {
                                        if (animation == 1) {
                                            return 3;
                                        }
                                        if (psx.conv(c_uint, animation -% 13) >= 14) {
                                            return 3;
                                        }
                                    }
                                }
                            }
                            break :halt;
                        }

                        psx.setBits(actor, "unk5AC_15", 0);
                        actor.unk1840 = psx.conv(i16, @divTrunc(distance, 4096));
                        if (distance >= @as(c_int, actor.unk5B9) << 12) {
                            i = @as(c_int, actor.unk1846);
                            i +%= @divTrunc(v.g.vs_gametime_tickspeed().*, 2);
                            actor.unk1842 = psx.conv(i16, direction);
                            if (i >= 0x100) {
                                i = 0xFF;
                            }
                            actor.unk1846 = psx.conv(u8, i);
                            actor.unk1847 = 0;
                        } else {
                            i = @as(c_int, actor.unk1847);
                            i +%= @divTrunc(v.g.vs_gametime_tickspeed().*, 2);
                            if (i >= 0x100) {
                                i = 0xFF;
                            }
                            actor.unk1847 = psx.conv(u8, i);
                        }
                        facing = direction;
                        if (input.?.pad == 1) {
                            facing = @as(c_int, input.?.vx);
                        }
                        facing &= 0xFFF;
                        if (distance != 0 and (statusWord(actor) & 0x180000) == 0) {
                            c.func_800A6660(@ptrCast(actor), facing, @ptrCast(input.?));
                        }
                        break :scale;
                    }
                    // halt:
                    speed = 0;
                }
                // stop:
                actor.unk0.unk34.vx = 0;
                actor.unk0.unk34.vz = 0;
                motion.vx = 0;
                motion.vz = 0;
                break :move;
            }
            // coast:
            distance = @as(c_int, actor.unk1840);
            direction = @as(c_int, actor.unk1842);
            distance -%= v.g.vs_gametime_tickspeed().*;
            actor.unk1840 = psx.conv(i16, distance);
            if (distance <= 0) {
                psx.setBits(actor, "unk5AC_15", 0);
                distance = 0;
            }
            distance <<= 12;
        }
        // scale:
        speed = distance;
        if (@as(c_int, actor.unk5BE) != 4) {
            speed = @divTrunc(distance *% @as(c_int, actor.unk5BE), 4);
            if (input.?.pad == 0 and speed > 0x1EFFF) {
                speed = 0x1F000;
            }
        }
        if (speed <= 0) {
            return done(actor, distance, direction, status);
        }
        const angle: c_int = 0xC00 -% direction;
        motion.vx = psx.conv(i16, @divTrunc(c.rcos(angle) *% speed, 0x1000000));
        motion.vz = psx.conv(i16, @divTrunc(c.rsin(angle) *% speed, 0x1000000));
    }

    // move:
    if ((statusWord(actor) & 0x70000) == 0x30000) {
        if (actor.unk0.position.vy > 0) {
            actor.unk6EE = 0;
            actor.unk6EF = 0;
        }
        motion.vx = psx.conv(i16, @as(c_int, motion.vx) +% @as(c_int, @as(i8, @bitCast(actor.unk6EE))));
        motion.vz = psx.conv(i16, @as(c_int, motion.vz) +% @as(c_int, @as(i8, @bitCast(actor.unk6EF))));
    }
    if ((flagsWord(actor) & 0x1000) != 0) {
        motion.vx = psx.conv(i16, @divTrunc(@as(c_int, motion.vx), 4));
        motion.vz = psx.conv(i16, @divTrunc(@as(c_int, motion.vz), 4));
    }
    v.g.D_800E90BC().* = 0;
    status = c.func_800A76BC(@ptrCast(actor), psx.cptr(c.SVECTOR, &motion), &result, @as(c_int, input.?.pad));
    switch (status) {
        0 => {
            distance = @divTrunc(speed, 4096);
        },
        1 => {
            distance = @as(c_int, motion.vx);
            if (distance < 0) {
                distance = -%distance;
            }
        },
        2 => {
            distance = @as(c_int, motion.vz);
            if (distance < 0) {
                distance = -%distance;
            }
        },
        3 => {
            distance = 0;
            if ((statusWord(actor) & 0x180000) == 0x100000) {
                return 3;
            }
            if (turning != 0) {
                psx.setBits(&actor.unk0, "unkA_0", 3);
            }
            if ((statusWord(actor) & 0x70000) != 0) {
                actor.unk0.unk34.vx = 0;
                actor.unk0.unk34.vz = 0;
                if ((statusWord(actor) & 0x70000) == 0x10000) {
                    _ = c.func_800A0204(index, 0x30, 0, 2);
                    psx.setBits(&actor.unk0, "unkA_0", 2);
                }
                return 3;
            }
            motion.vx = 0;
            motion.vz = 0;
        },
        else => {},
    }

    motion.vy = psx.conv(i16, c.func_800A6EE8(psx.cptr(c.SVECTOR, &actor.unk0.position), @as(c_int, motion.vx), @as(c_int, motion.vz), 1));
    motion.vy = psx.conv(i16, @as(c_int, motion.vy) -% @as(c_int, actor.unk0.position.vy));
    if ((statusWord(actor) & 0x1F0000) == 0) {
        previousArea = @as(c_int, psx.getBits(actor, "unk5AC_28"));
        psx.setBits(actor, "unk5AC_28", psx.conv(u4, v.g.D_800F49F8().*));
        psx.setBits(&actor.unk0, "unkC_0", psx.conv(u4, v.g.D_800F49F4().*));
    }
    if (input.?.pad == 0) {
        if (@as(c_int, motion.vy) < -0x20 or status == 3) {
            i = -%@divTrunc(@as(c_int, motion.vy), 64);
            if ((@as(c_int, motion.vy) & 0x3F) != 0) {
                i +%= 1;
            }
            if (result < i) {
                result = i;
            }
        } else if (result == 4) {
            motion.vy = psx.conv(i16, @as(c_int, motion.vy) +% @as(c_int, actor.unk0.position.vy));
            motion.vy = psx.conv(i16, @as(c_int, v.g.D_800F4B0A().*) -% @as(c_int, motion.vy));
            if (@as(c_int, motion.vy) >= -0xEC) {
                result = 3;
            }
        } else if (@as(c_int, v.g.D_800F4B0A().*) -% @as(c_int, actor.unk0.position.vy) < -0x5F and actor.unk0.position.vy < 0) {
            result = 2;
        }
        if (status == 3 and result >= 0) {
            result = 5;
        }
    }

    if ((flagsWord(actor) & 0x1000) != 0 and speed == 0) {
        if (@as(c_int, psx.byteAt(u16, actor, 0x18D6).*) == @as(c_int, actor.unk0.position.vx) +% @as(c_int, motion.vx) and
            @as(c_int, psx.byteAt(u16, actor, 0x18D8).*) == @as(c_int, actor.unk0.position.vz) +% @as(c_int, motion.vz))
        {
            distance = 0;
            return done(actor, distance, direction, status);
        }
        psx.byteAt(u16, actor, 0x18D6).* = psx.byteAt(u16, actor, 0x18D2).*;
        psx.byteAt(u16, actor, 0x18D8).* = psx.byteAt(u16, actor, 0x18D4).*;
        psx.byteAt(u16, actor, 0x18D2).* = psx.conv(u16, @as(c_int, actor.unk0.position.vx) +% @as(c_int, motion.vx));
        psx.byteAt(u16, actor, 0x18D4).* = psx.conv(u16, @as(c_int, actor.unk0.position.vz) +% @as(c_int, motion.vz));
    } else {
        const previousX: c_int = @as(c_int, psx.byteAt(u16, actor, 0x18D2).*);
        const previousZ: c_int = @as(c_int, psx.byteAt(u16, actor, 0x18D4).*);
        psx.byteAt(u16, actor, 0x18D2).* = 0;
        psx.byteAt(u16, actor, 0x18D4).* = 0;
        psx.byteAt(u16, actor, 0x18D6).* = psx.conv(u16, previousX);
        psx.byteAt(u16, actor, 0x18D8).* = psx.conv(u16, previousZ);
    }

    actor.unk5BE = 4;
    const state: c_uint = statusWord(actor) & 0x70000;
    if (state != 0) {
        if (state == 0x10000) {
            actor.unk0.unk34.vx = motion.vx;
            actor.unk0.unk34.vz = motion.vz;
            _ = c.func_800A0204(index, 0x30, 0, 4);
            psx.setBits(&actor.unk0, "unkA_0", 2);
            return 3;
        }
        if (result < 2) {
            if (motion.vy > 0) {
                actor.unk0.unk34.vx = motion.vx;
                actor.unk0.unk34.vz = motion.vz;
                return 3;
            }
            actor.unk0.unk34.vx = 0;
            actor.unk0.unk34.vz = 0;
            return 3;
        }
        if (result != 2) {
            if (result != 3) {
                if (result != 4) {
                    actor.unk0.unk34.vx = 0;
                    actor.unk0.unk34.vz = 0;
                    return 3;
                }
            }
        }
    }

    if (result > 0) {
        if (((v.g.D_800F49F0().*.?[0] >> 5) & 1) != 0 and result == 1) {
            actor.unk5BE = 2;
            c.func_800AA698(index, psx.cptr(c.SVECTOR, &motion), 0);
            return done(actor, distance, direction, status);
        }
        if (result >= 5) {
            actor.unk17FF = 0;
            return blocked(actor, turning, previousArea, direction);
        }
        if (result >= 2 and ((statusWord(actor) & 0x800000) != 0 or psx.getBits(actor, "unk5AC_3") != 0)) {
            return blocked(actor, turning, previousArea, direction);
        }
        if ((statusWord(actor) & 0x70000) == 0 and input.?.pad == 0 and result != 1 and actor.unk17FF < 8) {
            actor.unk17FF +%= 1;
            if ((statusWord(actor) & 0x180000) != 0x100000) {
                return blocked(actor, turning, previousArea, direction);
            }
            return 3;
        }
        saved = motion;
        const probe = v.g.D_800F4B08();
        motion.vx = psx.conv(i16, @as(c_int, probe.vx) -% @as(c_int, actor.unk0.position.vx));
        motion.vy = psx.conv(i16, @as(c_int, probe.vy) -% @as(c_int, actor.unk0.position.vy));
        motion.vz = psx.conv(i16, @as(c_int, probe.vz) -% @as(c_int, actor.unk0.position.vz));
        psx.setBits(&actor.unk0, "unkC_0", psx.conv(u4, probe.pad));
        if (result == 4) {
            if ((statusWord(actor) & 0x180000) == 0x100000) {
                fallStep(index, actor, &motion);
            } else {
                if (c.func_800A6798(@ptrCast(actor), psx.cptr(c.SVECTOR, &motion), psx.cptr(c.SVECTOR, &saved)) == 0) {
                    return reject(actor, turning, previousArea, direction);
                }
                psx.setBits(actor, "unk5AC_28", psx.conv(u4, @as(c_int, psx.conv(u16, probe.pad)) >> 8));
                actor.unk6EC = probe.vy;
                if ((flagsWord(actor) & 0xF0000000) != 0) {
                    const object: *v.D_800F45E0_t = v.g.D_800F45E0()[flagsWord(actor) >> 28].?;
                    if (object.unk1A == 0xFD) {
                        i = @as(c_int, object.unk6C[8].unk4);
                        while (i < 15) : (i +%= 1) {
                            const step: c_int = @as(c_int, @as(i8, @bitCast(psx.at(v.g.D_800E91AC(), i).*)));
                            motion.vy = psx.conv(i16, @as(c_int, motion.vy) +% step);
                            actor.unk6EC = psx.conv(i16, @as(c_int, actor.unk6EC) +% step);
                        }
                    }
                }
                if (c.func_800A9530(@ptrCast(actor), psx.cptr(c.SVECTOR, &motion)) == 0) {
                    return reject(actor, turning, previousArea, direction);
                }
                c.func_800AA600(index, psx.cptr(c.SVECTOR, &motion), 4);
                c.func_800AA850(index, psx.conv(c_short, @as(c_int, v.g.D_800F49F9().*) << 9), 4);
                actor.unk17FF = 0;
                actor.unk0.currentTileX = psx.conv(u8, @divTrunc(@as(c_int, actor.unk0.position.vx), 128));
                actor.unk0.currentTileZ = psx.conv(u8, @divTrunc(@as(c_int, actor.unk0.position.vz), 128));
                if ((psx.byteAt(c_int, actor, 0x60).* & 0xFF00FF) != (psx.byteAt(c_int, actor, 0x5C).* & 0xFF00FF) or
                    @as(c_int, actor.unk0.unk61) != @as(c_int, psx.getBits(&actor.unk0, "unkC_0")))
                {
                    actor.unk0.lastTouchedTileX = psx.conv(u8, @divTrunc(@as(c_int, probe.vx), 128));
                    actor.unk0.lastTouchedTileZ = psx.conv(u8, @divTrunc(@as(c_int, probe.vz), 128));
                    actor.unk0.unk61 = psx.conv(u8, psx.getBits(&actor.unk0, "unkC_0"));
                }
            }
        } else if ((statusWord(actor) & 0x180000) == 0x100000) {
            fallStep(index, actor, &motion);
        } else {
            if (input.?.pad == 0) {
                if (psx.conv(u16, @as(c_int, motion.vy) +% 0xFF) >= 0x100) {
                    return blocked(actor, turning, previousArea, direction);
                }
                if (distance == 0) {
                    return blocked(actor, turning, previousArea, direction);
                }
            }
            if (result >= 2) {
                if ((statusWord(actor) & 0x70000) != 0 and (flagsWord(actor) & 0x1000) != 0) {
                    return reject(actor, turning, previousArea, direction);
                }
                if (c.func_800A69B4(@ptrCast(actor)) == 0) {
                    return reject(actor, turning, previousArea, direction);
                }
            }
            c.func_800AA600(index, psx.cptr(c.SVECTOR, &motion), result *% 8);
        }
        if (result == 1) {
            animation = actor.animationId;
            if (animation >= 100) {
                animation -= 100;
            }
            if (psx.conv(c_uint, animation -% 13) >= 2 and animation != 0x13 and animation != 0x14 and animation != 0x54) {
                _ = c.func_800A0204(index, @as(c_int, v.g.D_800E90C4().*), 0, 4);
            }
            psx.setBits(&actor.unk0, "unkA_3", 1);
            actor.unk1800 = psx.conv(i16, (@as(c_int, actor.unk0.facing) +% @as(c_int, actor.unk0.unk3E)) & 0xFFF);
            psx.setBits(&actor.unk0, "unk9_6", 1);
        } else if ((statusWord(actor) & 0x180000) == 0x100000) {
            _ = c.func_800A0204(index, @as(c_int, v.g.D_800E90C6().*), 0, 4);
            if (index == 0) {
                c.func_800AE6C0(@ptrCast(actor), 0x2A, 0x2B);
            } else {
                c.func_800AE4FC(@ptrCast(&actor.unk0), 0xC);
            }
            c.func_800A6660(@ptrCast(actor), facing, @ptrCast(input.?));
        } else {
            if (result == 4) {
                psx.setBits(&actor.unk0, "unkA_3", 2);
            }
            _ = c.func_800A0204(index, @as(c_int, psx.at(v.g.D_800E90C3(), result).*), 0, 4);
            actor.unk1800 = psx.conv(i16, (@as(c_int, actor.unk0.facing) +% @as(c_int, actor.unk0.unk3E)) & 0xFFF);
            if (index == 0) {
                c.func_800AE6C0(@ptrCast(actor), 0x29, 0);
            } else {
                c.func_800AE4FC(@ptrCast(&actor.unk0), 0xC);
            }
        }
        actor.unk0.unk11 = 0;
        actor.unk6EE = 0;
        actor.unk6EF = 0;
        psx.setBits(&actor.unk0, "unkA_0", 0);
        return 3;
    }

    if (motion.vy > 0) {
        if ((statusWord(actor) & 0x180000) == 0x100000) {
            motion.vx = 0;
            motion.vz = 0;
            return fall(actor);
        }
        if ((areaWord(actor) & 0xF) != 0) {
            _ = c.func_800A9378(@as(c_int, actor.unk0.position.vx), @as(c_int, actor.unk0.position.vy), @as(c_int, actor.unk0.position.vz), 0);
            psx.setBits(&actor.unk0, "unkC_0", psx.conv(u4, v.g.D_800F49F4().*));
        }
        if (motion.vy >= 0x40) {
            if (input.?.pad == 0) {
                if (motion.vy >= 0x100) {
                    if ((statusWord(actor) & 0x180000) != 0x100000 and actor.unk17FF < 8) {
                        actor.unk17FF +%= 1;
                        return blocked(actor, turning, previousArea, direction);
                    }
                    actor.unk0.unk11 = 0;
                }
            } else {
                c.func_800A3054(@ptrCast(actor), @ptrCast(&motion));
            }
            actor.unk0.unk34.vx = motion.vx;
            actor.unk0.unk34.vz = motion.vz;
            actor.unk0.unk34.vy = 0;
            _ = c.func_800A0204(index, 0x2F, 0, 4);
            psx.setBits(&actor.unk0, "unkA_0", 3);
            const flags = psx.byteAt(v.vs_battleMovementModeFlags, actor, 0x5B0);
            psx.setBits(flags, "fast", 0);
            psx.setBits(flags, "mode", 1);
            if (distance >= @as(c_int, actor.unk5B9)) {
                psx.setBits(flags, "mode", 2);
                psx.setBits(flags, "fast", 1);
            }
            psx.setBits(&actor.unk0, "unkC_0", 0);
            return 3;
        }
        actor.unk17FF = 0;
        if (((v.g.D_800F49F0().*.?[0] >> 5) & 1) == 0) {
            actor.unk5BE = 5;
        }
    } else {
        actor.unk6EC = 0;
        psx.setBits(&actor.unk0, "unkA_3", 0);
        psx.setBits(&actor.unk0, "unk9_6", 0);
        if (motion.vy < -2) {
            actor.unk5BE = 2;
        }
        actor.unk17FF = 0;
    }
    // land:
    c.func_800AA698(index, psx.cptr(c.SVECTOR, &motion), 0);
    return done(actor, distance, direction, status);
}
