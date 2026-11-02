//! An event script opcode that starts a D_800F4B30 slot's move (src/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const v = @import("views_4A0A8");

/// orig: func_800B70E8 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Starts slot script[5] >> 6's move from its D_800EB9BC entry toward the script's targets; returns 1
/// while the slot is still busy, 0 otherwise (or with no D_800EB9BC).
pub fn func_800B70E8(script: [*c]u8, _: c_short) callconv(.c) c_int {
    const base = v.g.D_800EB9BC().*;
    const k: usize = script[5] >> 6;
    const slot = &v.g.D_800F4B30()[k];
    const from_base = base orelse return 0;
    const from: *v.D_800EB9BC_t = &@as([*]v.D_800EB9BC_t, @ptrCast(from_base))[k];
    if (slot.unk3 != 0) return 1;
    slot.unk4 = from.unk8;
    slot.unk8 = psx.conv(i16, @as(c_int, psx.conv(i8, script[1])) *% 2 -% slot.unk4);
    slot.unk6 = from.unkA;
    slot.unkA = psx.conv(i16, @as(c_int, psx.conv(i8, script[2])) *% 2 -% slot.unk6);
    slot.unkC = from.unkC;
    slot.unkE = psx.conv(i16, (@as(c_int, script[3]) << 7) -% slot.unkC);
    slot.unk10 = @as([*]u8, @ptrCast(from))[3];
    slot.unk12 = psx.conv(i16, @as(c_int, script[4]) -% slot.unk10);
    slot.unk1 = script[5] & 7;
    slot.unk3 = script[6];
    slot.unk2 = 0;
    return 0;
}
