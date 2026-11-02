//! The room's event triggers (src/BATTLE/BATTLE.PRG/4A0A8.c).
const std = @import("std");
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// Byte `name` (unk0_0: the cell's x, unk0_16: its z, unk0_24) of an actor's position record.
fn cell(position: *v.func_8006EBF8_t, comptime name: []const u8) c_int {
    return psx.getBits(&position.unk0.unk0.fields, name);
}

/// A trigger's actor reference (getShort(p + 1)) resolved by func_800BFE50.
fn actorOf(p: [*]u8) i16 {
    return psx.conv(i16, c.func_800BFE50(psx.conv(u16, c.vs_battle_getShort(p + 1))));
}

/// orig: func_800BEC58 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Asks whether an event starts: 0xFF whether a script runs (2) or not (0); 1 the room's own script;
/// 0x40 event file arg1; else, unless a script runs, the first of the room's 16 triggers whose
/// type mask has bits of arg0 and whose conditions (cells, flags, actors, items, heights; 0xFE
/// negates the next) all hold starts its event (1); none: 0 at the list's end marker.
pub fn func_800BEC58(arg0: c_int, arg1: c_int, arg2: c_int, arg3: c_int) callconv(.c) c_short {
    var buf: [64]u8 = undefined; // sprintf's target, never read
    var position = std.mem.zeroes(v.func_8006EBF8_t); // the C's stack record, written by the callees
    const kind: i16 = psx.conv(i16, arg0);
    const mode: i16 = psx.conv(i16, arg3);
    if (kind == 0xFF) {
        if (v.g.D_800F4C34().* == 0) return 0;
        return 2;
    }
    if (kind == 1) {
        const scriptSection: c_int = v.g._mpdScriptSection().*;
        if ((scriptSection & 1) != 0) {
            v.g._mpdScriptSection().* = 0;
            v.g.D_800F4C34().* = 1;
            v.g.D_800F4BE0().* = 1;
            v.g.D_800F4BA8().* = 1;
            return 1;
        }
    }
    if (kind == 0x40) {
        v.g._evtFile().* = psx.conv(i16, arg1);
        v.g.D_800F4BE0().* = 0;
        v.g.D_800F4C34().* = 1;
        v.g.D_800F4BA8().* = 1;
        return 1;
    }
    if (v.g.D_800F4C34().* != 0 and v.g.D_800F4BA8().* != 0) return 2;

    // BUG in the original: no return after the 16th trigger. Both objects (32- and 64-bit) return
    // the last trigger's type byte when its mask missed, else its last condition's result, or the
    // actor id 1 when a reference resolved to none.
    var last: c_int = 0;
    var i: usize = 0;
    while (i < 16) : (i += 1) {
        const section: [*]u8 = @ptrCast(v.g._mpdClearedSection().*.?);
        const offset = psx.byteAt(u16, section, @intCast(2 * i)).*;
        if (offset == 0xFFFF) return 0;
        var p: [*]u8 = section + offset;
        if ((@as(c_int, p[0]) & kind) == 0) {
            last = p[0];
            continue;
        }
        p += 1;
        const eventId = c.vs_battle_getShort(p);
        p += 2;
        var negate: c_int = 0;
        while (true) {
            var result: c_int = 0;
            var noActor = false;
            switch (p[0]) {
                0xFF => {
                    if ((@as(c_int, eventId) & 0x8000) != 0) {
                        v.g._evtFile().* = eventId & 0xF;
                        v.g.D_800F4BE0().* = 2;
                    } else {
                        v.g._evtFile().* = eventId;
                        v.g.D_800F4BE0().* = 0;
                    }
                    v.g.D_800F4C34().* = 1;
                    v.g.D_800F4BA8().* = 0;
                    return 1;
                },
                1 => {
                    _ = c.func_800A1108(0, &position);
                    if (cell(&position, "unk0_0") == p[1] and cell(&position, "unk0_16") == p[2]) result = 1;
                    p += 3;
                },
                2 => {
                    _ = c.func_800A1108(0, &position);
                    if (cell(&position, "unk0_0") >= p[1] and cell(&position, "unk0_0") < @as(c_int, p[1]) + p[3] and
                        cell(&position, "unk0_16") >= p[2] and cell(&position, "unk0_16") < @as(c_int, p[2]) + p[4]) result = 1;
                    p += 5;
                },
                3, 15 => {
                    const left: c_int = c.vs_battle_getStateFlag(psx.conv(i16, ((@as(c_int, p[1]) & 0xF) << 6) | (p[2] >> 2)));
                    const right: c_int = if (p[0] == 3)
                        p[3]
                    else
                        c.vs_battle_getStateFlag(psx.conv(i16, ((@as(c_int, p[2]) & 3) << 8) | p[3]));
                    switch (p[1] & 0xF0) {
                        0x00 => result = @intFromBool(left <= right),
                        0x10 => result = @intFromBool(left >= right),
                        0x20 => result = @intFromBool(left == right),
                        0x30 => result = @intFromBool(left != right),
                        0x40 => result = @intFromBool(left < right),
                        0x50 => result = @intFromBool(left > right),
                        else => {},
                    }
                    p += 4;
                },
                4 => {
                    const left = c.vs_battle_getShort(p + 1);
                    if ((left & 0x1000) != 0) {
                        if (c.func_8007CF64(left & 0x1F) != null) result = 1;
                    } else if (c.func_8007CF18(left) != 1) {
                        result = 1;
                    }
                    p += 3;
                },
                5 => {
                    const flagValue = psx.conv(i16, c.func_8007CF80(p[1]));
                    const flagId = c.vs_battle_getShort(p + 2);
                    if (flagId != 0) {
                        c.vs_battle_setStateFlag(flagId, psx.conv(u8, flagValue));
                        result = 1;
                    } else if (flagValue != 0) {
                        result = 1;
                    }
                    p += 4;
                },
                6 => {
                    if (mode == 0) {
                        const actorId = actorOf(p);
                        const otherActorId = actorOf(p + 2);
                        if (actorId != 1 and otherActorId != 1 and arg1 == actorId and arg2 == otherActorId) result = 1;
                    }
                    p += 5;
                },
                7 => blk: {
                    const actorId = actorOf(p);
                    if (actorId == 1) {
                        negate = 0;
                        noActor = true;
                        break :blk;
                    }
                    const flagValue = psx.conv(i16, c.func_8007C218(actorId));
                    const flagId = c.vs_battle_getShort(p + 3);
                    if (flagId != 0) {
                        c.vs_battle_setStateFlag(flagId, psx.conv(u8, flagValue));
                        result = 1;
                    } else if (flagValue != 0) {
                        result = 1;
                    }
                    p += 5;
                },
                8 => {
                    if (mode == 1) {
                        const data: [*]u8 = @ptrFromInt(@as(usize, psx.conv(u32, arg2))); // psx_to_ptr(u_char*, arg2)
                        if (data[0] == p[1] and data[2] == p[2]) result = 1;
                    }
                    p += 3;
                },
                9 => {
                    if ((c.func_800A0BE0(0) & 0x104000) == 0) result = 1;
                    p += 1;
                },
                10 => {
                    const flagValue = psx.conv(i16, c.vs_battle_itemIdIsInInventory(c.vs_battle_getShort(p + 1)));
                    const flagId = c.vs_battle_getShort(p + 3);
                    if (flagId != 0) {
                        c.vs_battle_setStateFlag(flagId, psx.conv(u8, flagValue));
                        result = 1;
                    } else if (flagValue != 0) {
                        result = 1;
                    }
                    p += 5;
                },
                11 => {
                    if (psx.conv(i16, c.func_800A0024(p[1], &position)) >= 0 and
                        cell(&position, "unk0_0") == p[2] and
                        cell(&position, "unk0_16") == p[3] and
                        (p[4] == 0 or -%@divTrunc(@as(c_int, position.unk0.unk4.vy), 128) +% 1 == p[4]) and
                        (p[5] == 0 or (cell(&position, "unk0_24") >> 2) + 1 == p[5])) result = 1;
                    p += 6;
                },
                12 => {
                    const flagValue = psx.conv(i16, @divTrunc(-%c.func_800A92B8(p[1], p[2]), 128));
                    const flagId = c.vs_battle_getShort(p + 3);
                    if (flagId != 0) {
                        c.vs_battle_setStateFlag(flagId, psx.conv(u8, flagValue));
                        result = 1;
                    } else if (flagValue != 0) {
                        result = 1;
                    }
                    p += 5;
                },
                13 => blk: {
                    const actorId = actorOf(p);
                    if (actorId == 1) {
                        negate = 0;
                        noActor = true;
                        break :blk;
                    }
                    _ = c.func_800A1108(actorId, &position);
                    if (cell(&position, "unk0_0") == p[3] and cell(&position, "unk0_16") == p[4]) result = 1;
                    p += 5;
                },
                14 => blk: {
                    const actorId = actorOf(p);
                    if (actorId == 1) {
                        negate = 0;
                        noActor = true;
                        break :blk;
                    }
                    _ = c.func_800A1108(actorId, &position);
                    if (cell(&position, "unk0_0") >= p[3] and cell(&position, "unk0_0") < @as(c_int, p[3]) + p[5] and
                        cell(&position, "unk0_16") >= p[4] and cell(&position, "unk0_16") < @as(c_int, p[4]) + p[6]) result = 1;
                    p += 7;
                },
                16 => blk: {
                    const actorId = actorOf(p);
                    if (actorId == 1) {
                        negate = 0;
                        noActor = true;
                        break :blk;
                    }
                    if (c.func_8007CFCC(actorId) != 0) result = 1;
                    p += 3;
                },
                0xFE => {
                    negate = 1;
                    p += 1;
                    continue;
                },
                else => _ = c.sprintf(&buf, @constCast("UNKNOWN TRIGGER DETECTED:%02x"), @as(c_int, p[0])),
            }
            if (result == negate) {
                last = if (noActor) 1 else result;
                break;
            }
            negate = 0;
        }
    }
    return psx.conv(i16, last);
}
