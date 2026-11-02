//! Writes a six-byte packet record into the slot its byte 1 selects, then calls the
//! follow-up routine for that slot (src/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800B7014 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Stores a six-byte packet record in the slot chosen by its byte 1 and runs the slot's follow-up.
pub fn func_800B7014(packet: [*c]u8, _: c_short) callconv(.c) c_int {
    const head = v.g.D_800EB9BC().*;
    if (head == null) return 0;

    // packet[1] >> 6 is 0..3: the slot index into both tables.
    const slot: c_int = @as(c_int, packet[1]) >> 6;

    psx.at(v.g.D_800F4B30(), slot).unk3 = 0;

    const entry = psx.at(head.?, slot);
    psx.setBits(entry, "unk0_3", psx.conv(u3, @as(c_int, packet[1]) & 3));
    // (signed char) packet bytes, doubled in int, then stored as short.
    entry.unk8 = psx.conv(i16, @as(c_int, psx.conv(i8, packet[2])) *% 2);
    entry.unkA = psx.conv(i16, @as(c_int, psx.conv(i8, packet[3])) *% 2);
    entry.unkC = psx.conv(i16, @as(c_int, packet[4]) << 7);
    psx.setBits(entry, "unk0_24", packet[5]);

    c.func_800C8550(psx.conv(c_uint, slot), entry, packet);
    return 0;
}
