//! An actor's step onto a cell: the cell's event, object or door, and falling out of the map (src/BATTLE/BATTLE.PRG/146C.c).
const std = @import("std");
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: func_80074BAC (src/BATTLE/BATTLE.PRG/146C.c)
/// When the actor (not frozen, 0x1000000, and with no screen transition running) stands on a new cell (its word
/// differs from the one kept in unk2C, the falling flag changed, or it is lifted), outside battle state 3: a cell
/// event (unk0_13) that func_800BEC58(2) accepts starts, an object cell (unk0_16) not under a busy actor runs its
/// object (func_80073F7C; a 0x10 object is a pickup, func_8006F4B0), a door cell (unk0_14) takes the first open
/// door (its flag up to its level) into this zone or another; then the cell is kept. A deep cell (unk0_17) under
/// a raised actor warps to func_8008D418's location, or takes 20 HP (fatal under 21) and goes back to the door.
/// Returns 1 when something happened.
pub fn func_80074BAC(actorId: c_int, cell: [*c]u8) callconv(.c) c_int {
    // The C's local door is partly written: the bytes it leaves are 0 here (PORTING.md §11.4).
    var door = std.mem.zeroes(v._mpdRoomDoorSection_t);
    const entered = false;
    var status: c_int = @bitCast(c.func_800A0BE0(actorId));
    const falling: c_int = @intFromBool((status & 0x4000) != 0);
    const position: *v.func_8006EBF8_t3 = @ptrCast(@alignCast(cell));
    var room: ?*v._mpdRoomSection3 = undefined;
    if (status & 0x70000 != 0) {
        room = @ptrCast(c.func_8008B764(0, 0, (status & 0x70000) >> 16));
    } else {
        room = @ptrCast(c.func_8008B764(cell[0], cell[2], cell[1]));
    }
    if (v.g.vs_battle_screenTransitionStep().* != 0) {
        return 0;
    }
    if (status & 0x1000000 != 0) {
        return 0;
    }
    const actor = psx.at(v.g.vs_battle_actors(), actorId).*.?;
    const word = psx.byteAt(i32, cell, 0);
    if ((word.* & 0xFFFFFF) != (actor.unk2C.s32 & 0xFFFFFF) or falling != actor.unk2C.u8[3] or (status & 0x70000) != 0) {
        if (v.g.D_800F196C().* != 3) {
            if (psx.getBits(room.?, "unk0_13") != 0 and c.func_800BEC58(2, 0, 0, 0) == 1) {
                c.func_80073898();
                actor.unk2C.s32 = word.*;
                actor.unk2C.u8[3] = @intCast(falling); // 0 or 1
                if (psx.getBits(room.?, "unk0_16") != 0) {
                    const object = c.func_8008D438(cell[0], cell[2], cell[1]);
                    if (object != null and object.*.unk6 == 0x10) {
                        c.func_8006F4B0(@ptrCast(position));
                    }
                }
                c.func_800A0ABC(actorId);
                return 1;
            }
            if (psx.getBits(room.?, "unk0_16") != 0 and (status & 0x504000) == 0) {
                const object = c.func_8008D438(cell[0], cell[2], cell[1]);
                if (object != null) {
                    if (object.*.unk6 != 0x10) {
                        c.func_80073F7C(@ptrCast(object), @ptrCast(position), actorId);
                        actor.unk2C.s32 = word.*;
                        actor.unk2C.u8[3] = @intCast(falling); // 0 or 1
                        return 1;
                    }
                    c.func_8006F4B0(@ptrCast(position));
                }
            }
            if (psx.getBits(room.?, "unk0_14") != 0 and (status & 0x4000) == 0) {
                var doors: ?*v.D_800F1D08_t = undefined;
                if (status & 0x70000 != 0) {
                    if (status & 0x20000000 != 0) {
                        doors = null;
                    } else {
                        doors = @ptrCast(c.func_8008BAC8(0, 0, (status & 0x70000) >> 16));
                    }
                } else {
                    doors = @ptrCast(c.func_8008BAC8(cell[0], cell[2], cell[1]));
                }
                if (doors) |list| {
                    var i: c_int = 0;
                    while (i < list.count) : (i += 1) { // i < an int
                        const entry = psx.get(psx.at(&list.values, i).*).?;
                        if (c.vs_battle_getStateFlag(psx.conv(c_short, psx.getBits(entry, "unk6_0"))) >= psx.getBits(entry, "unk4_0")) {
                            if (@as(c_int, entry.zndId) == v.g._zoneContext().zndId) {
                                _ = c.func_8007C4E0(@ptrCast(entry), 0, 8);
                            } else {
                                _ = c.func_8007C5C0(@ptrCast(entry), 0, 8);
                            }
                            c.func_800A0ABC(actorId);
                            c.func_8008B8F8(@ptrCast(psx.get(psx.at(&list.values, i).*)));
                            return 1;
                        }
                    }
                }
            }
        }
        if ((status & 0x104000) == 0) {
            actor.unk2C.s32 = word.*;
        }
        actor.unk2C.u8[3] = @intCast(falling); // 0 or 1
    }
    if (!entered and psx.getBits(room.?, "unk0_17") != 0 and position.unk4.vy > 16) {
        status = c.func_8008D418();
        if (status != -1 and status != 0) {
            door.zndId = psx.conv(u8, @divTrunc(status, 65536));
            door.mpdId = psx.conv(u8, @rem(status, 65536));
            psx.setBits(&door.unk2, "unk0_0", psx.conv(u5, cell[0]));
            psx.setBits(&door, "unk4_8", psx.getBits(&door.unk2, "unk0_0"));
            psx.setBits(&door.unk2, "unk0_5", psx.conv(u5, cell[2]));
            psx.setBits(&door, "unk6_10", psx.getBits(&door.unk2, "unk0_5"));
            psx.setBits(&door.unk2, "unk0_10", psx.conv(u3, cell[1]));
            psx.setBits(&door.unk2, "unk0_13", 7);
            psx.setBits(&door, "unk4_13", psx.conv(u2, cell[3] >> 6));
            psx.setBits(&door, "unk4_15", 0);
            psx.setBits(&door, "unk6_15", 0);
            if (@as(c_int, door.zndId) == v.g._zoneContext().zndId) {
                _ = c.func_8007C4E0(@ptrCast(&door), 0, 8);
            } else if (psx.get(actor.stats).?.unk957 != 0) {
                _ = c.func_8007C5C0(@ptrCast(&door), 0, 8);
            } else {
                _ = c.func_8007C5C0(@ptrCast(&door), 0, 8);
            }
            c.func_800A0ABC(actorId);
            c.func_8008B8F8(null);
            return 0;
        }
        const stats = psx.get(actor.stats).?;
        if (stats.currentHP < 21) {
            if (stats.currentHP != 0) {
                c.func_80093B68(stats.unk957, 0, @bitCast(@as(c_uint, 0x80000014)), 0);
            }
            psx.get(actor.stats).?.currentHP = 0;
            _ = c.func_80074A20(psx.get(actor.stats).?.unk957);
            return 1;
        }
        stats.currentHP = psx.conv(i16, @as(c_int, stats.currentHP) - 20); // at least 21
        c.vs_battle_setDoorEntered();
        _ = c.func_8007C4E0(@ptrCast(v.g.D_800F1AB0()), 0, 8);
        c.func_80093B68(psx.get(actor.stats).?.unk957, 0, @bitCast(@as(c_uint, 0x80000014)), 0);
        c.func_800A0ABC(actorId);
        c.func_8008B8F8(@ptrCast(v.g.D_800F1AB0()));
        return 1;
    }
    return 0;
}
