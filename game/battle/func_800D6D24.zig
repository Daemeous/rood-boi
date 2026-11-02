//! Steps a P-file block 2 sequence (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");

/// orig: func_800D6D24 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Counts the current frame down by 2; at 0 runs commands: 0 shows a block 1 frame for a time, 1 sets an offset, 2 restarts, 3 stops.
pub fn func_800D6D24(sequence: [*c]v.func_800D6CF_t) callconv(.c) void {
    const s: *v.func_800D6CF_t = sequence;
    if (s.unk6 != 0) {
        s.unk6 -%= 2;
        if (s.unk6 != 0) {
            return;
        }
    }
    while (true) {
        const word: c_int = @as(*align(1) c_int, @ptrFromInt(@intFromPtr(psx.get(s.unk18).?) +% s.unkE)).*;
        switch (@as(c_uint, @bitCast(word)) & 0xF0000000) {
            0 => {
                const table = psx.get(psx.at(&v.g.D_800F569C().*.?.block1Tables, s.unk16).*); // block1Tables[4]: unk16 is not checked
                psx.put(&s.unk1C, @as(*v.pFileBlock1Indices, @ptrFromInt(@intFromPtr(table) +% @as(usize, @intCast(word & 0xFFFF)) * @sizeOf(v.pFileBlock1Indices))));
                @as(*align(1) i16, @ptrCast(&s.unk6)).* = psx.conv(i16, (word >> 16) & 0x3FFF);
                s.unkE +%= 4;
                return;
            },
            0x10000000 => {
                s.unk8 = psx.conv(i16, (word << 18) >> 18);
                s.unkA = psx.conv(i16, (word << 4) >> 18);
                s.unkE +%= 4;
            },
            0x20000000 => s.unkE = 0,
            0x30000000 => return,
            else => {},
        }
    }
}
