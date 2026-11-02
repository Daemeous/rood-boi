//! The room's section-12 entries not flagged unk0_9, as tile points (src/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");

/// orig: D_80068BEC (146C.c's file `static short const`, in .rodata: not compared state, PORTING.md §6.3).
/// Its 7 entries and the 0 after them: the C reads index 7 (D_800F1D70's counters stop at 7), past the table,
/// into the padding before the next table (0 in BATTLE.PRG and in the PC builds' rodata).
const heights = [8]i16{ 8, 14, 16, 11, 7, 3, 1, 0 };

/// orig: func_8008D2C0 (src/BATTLE/BATTLE.PRG/146C.c)
/// Writes one point per section-12 entry without unk0_9 (its unkA8 and unkB0 over 4096, the height
/// D_80068BEC[D_800F1D70[i]] less unkAC over 4096, and the entry index) and returns how many; 0 with no section.
pub fn func_8008D2C0(points: [*c]v.func_8008D2C0_t) callconv(.c) c_int {
    var point = points;
    const room = v.g.vs_battle_roomData();
    if (psx.get(room.section12)) |first| {
        var entry: [*]v._mpdRoomSection12 = @ptrCast(first);
        const count = psx.conv(c_int, room.header.section12Len / 184);
        var found: c_int = 0;
        var i: c_int = 0;
        while (i < count) : ({
            i +%= 1;
            entry += 1;
        }) {
            if (psx.getBits(&entry[0], "unk0_9") == 0) {
                point.*.unk0 = psx.conv(i16, @divTrunc(entry[0].unkA8, 4096));
                const y = @divTrunc(entry[0].unkAC, 4096);
                // D_800F1D70's counters are 0..7: larger would read further past the table (the port traps).
                point.*.unk1 = psx.conv(i16, @as(c_int, heights[v.g.D_800F1D70()[@intCast(i)]]) -% y);
                point.*.unk2 = psx.conv(i16, @divTrunc(entry[0].unkB0, 4096));
                point.*.unk3 = psx.conv(i16, i);
                point += 1;
                found +%= 1;
            }
        }
        return found;
    }
    return 0;
}
