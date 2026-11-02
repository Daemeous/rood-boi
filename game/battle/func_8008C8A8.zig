//! The room's moving objects (section 12) stepped along their paths (src/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: D_80068BEC (146C.c's file `static short const`, in .rodata: not compared state, PORTING.md §6.3).
/// Its 7 entries and the 0 after them: the C reads index 7 (D_800F1D70's counters stop at 7), past the table,
/// into the padding before the next table (0 in BATTLE.PRG and in the PC builds' rodata).
const heights = [8]i16{ 8, 14, 16, 11, 7, 3, 1, 0 };

/// orig: func_8008C8A8 (src/BATTLE/BATTLE.PRG/146C.c)
/// While the lights timer D_800F1D68 runs, counts it down (flickering the lights under 30 on odd frames, off at 0)
/// and nothing else. Otherwise each section-12 object not hidden (unk0_9) and not parked moves per tick: rising to
/// height 7, then either sliding by its leg's velocity (backed out when func_800A4494 reports a collision; at the
/// leg's length it snaps to its next stop) or pausing at the stop (then turning to the next leg, ping-ponging
/// between stop 0 and unk0_10). Its scene object (func_8009195C) follows it, or is hidden (0x100).
pub fn func_8008C8A8() callconv(.c) c_int {
    var point: v.SVECTOR = undefined;
    const room = v.g.vs_battle_roomData();
    if (v.g.D_800F1BA4().* != 0 or psx.get(room.section12) == null or @as(u8, @bitCast(v.g.D_800F1D6E().*)) != 0) {
        return 0; // a bare `return;` in the C: its object returns 0
    }
    const ticks = v.g.vs_gametime_tickspeed().* >> 1;
    if (v.g.D_800F1D68().* > 0) {
        v.g.D_800F1D68().* = psx.conv(i16, @as(c_int, v.g.D_800F1D68().*) -% ticks);
        if (v.g.D_800F1D68().* < 30 and v.g.vs_main_frameBuf().* != 0) {
            c.func_8008E4DC(@intFromBool(v.g.D_800F1D6F().* == 0));
        }
        if (v.g.D_800F1D68().* <= 0) {
            c.func_8008E4DC(0);
            v.g.D_800F1D68().* = 0;
        }
        return 0; // a bare `return;` in the C: its object returns 0
    }
    var object: [*]v._mpdRoomSection12 = @ptrCast(psx.get(room.section12).?);
    const count = psx.conv(c_int, room.header.section12Len / @sizeOf(v._mpdRoomSection12));
    const counters = v.g.D_800F1D70();
    var i: c_int = 0;
    while (i < count) : ({
        i += 1; // i < count
        object += 1;
    }) {
        const o = &object[0];
        if (psx.getBits(o, "unk0_9") == 0) {
            if (psx.getBits(o, "unk0_8") == 0 or (psx.byteAt(u32, o, 4).* & 0xFE) != 0xFE) {
                var tick: c_int = 0;
                while (tick < (v.g.vs_gametime_tickspeed().* >> 1)) : (tick += 1) { // tick < an int
                    const height: c_uint = counters[@intCast(i)];
                    if (height < 7) {
                        counters[@intCast(i)] = psx.conv(u8, height +% psx.conv(c_uint, v.g.vs_gametime_tickspeed().* >> 1));
                        if (counters[@intCast(i)] >= 8) {
                            counters[@intCast(i)] = 7;
                        }
                    }
                    // velocities has 6 entries; unk0_29 is 3 bits: the C does not bound it.
                    const motion: *v.RoomMotionVectors = @ptrCast(o);
                    if (psx.getBits(o, "unk4_0") == 0) {
                        const velocity = psx.at(&motion.velocities, psx.getBits(o, "unk0_29"));
                        o.unkA8 +%= velocity.vx;
                        o.unkAC +%= velocity.vy;
                        o.unkB0 +%= velocity.vz;
                        point.vy = psx.conv(i16, -%@divTrunc(o.unkAC, 4096));
                        point.vx = psx.conv(i16, @divTrunc(o.unkA8, 4096));
                        point.vz = psx.conv(i16, @divTrunc(o.unkB0, 4096));
                        if (c.func_800A4494(i +% 2, @ptrCast(&point)) != 0) {
                            const back = psx.at(&motion.velocities, psx.getBits(o, "unk0_29"));
                            o.unkA8 -%= back.vx;
                            o.unkAC -%= back.vy;
                            o.unkB0 -%= back.vz;
                        } else {
                            psx.setBits(o, "unk4_16", psx.getBits(o, "unk4_16") +% 1);
                            if (@as(c_int, psx.getBits(o, "unk4_16")) >= psx.at(&motion.velocities, psx.getBits(o, "unk0_29")).pad) {
                                psx.setBits(o, "unk4_16", 0);
                                psx.setBits(o, "unk4_0", 1);
                                o.unkA8 = o.unk8[psx.getBits(o, "unk0_27")].vx;
                                o.unkAC = o.unk8[psx.getBits(o, "unk0_27")].vy;
                                o.unkB0 = o.unk8[psx.getBits(o, "unk0_27")].vz;
                                if (psx.getBits(o, "unk0_27") == psx.getBits(o, "unk4_8")) {
                                    @as(*u32, @ptrCast(&o.unkB4)).* |= 0x4000;
                                }
                            }
                        }
                    } else {
                        psx.setBits(o, "unk4_16", psx.getBits(o, "unk4_16") +% 1);
                        if (@as(c_int, psx.getBits(o, "unk4_16")) >= o.unk8[psx.getBits(o, "unk0_27")].pad) {
                            psx.setBits(o, "unk4_16", 0);
                            psx.setBits(o, "unk4_0", 0);
                            psx.setBits(o, "unk0_14", psx.getBits(o, "unk0_27"));
                            psx.setBits(o, "unk0_29", psx.getBits(o, "unk0_29") +% 1);
                            @as(*u32, @ptrCast(&o.unkB4)).* &= ~@as(u32, 0x4000);
                            if (psx.getBits(o, "unk0_14") == 0) {
                                psx.setBits(o, "unk0_26", 1);
                                psx.setBits(o, "unk0_29", 0);
                            } else if (psx.getBits(o, "unk0_14") == psx.getBits(o, "unk0_10")) {
                                psx.setBits(o, "unk0_26", 0);
                            }
                            if (psx.getBits(o, "unk0_26") != 0) {
                                psx.setBits(o, "unk0_27", psx.getBits(o, "unk0_27") +% 1);
                            } else {
                                psx.setBits(o, "unk0_27", psx.getBits(o, "unk0_27") -% 1);
                            }
                        }
                    }
                }
            }
            const scene = c.func_8009195C(psx.getBits(o, "unk0_0"));
            if (scene != null) {
                scene[1] = @divTrunc(o.unkA8, 4096);
                const y = @divTrunc(o.unkAC, 4096);
                // D_800F1D70's counters are 0..7: larger would read further past the table (the port traps).
                const lifted = psx.conv(c_uint, @as(c_int, heights[counters[@intCast(i)]]) -% y);
                scene[2] = @bitCast(lifted);
                scene[3] = @divTrunc(o.unkB0, 4096);
                scene[0] &= ~@as(c_int, 0x100);
            }
        } else {
            const scene = c.func_8009195C(psx.getBits(o, "unk0_0"));
            if (scene != null) {
                scene[0] |= 0x100;
            }
        }
    }
    return 0; // the C falls off its end: its object returns 0
}
