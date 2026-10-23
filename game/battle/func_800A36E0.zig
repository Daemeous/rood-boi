//! Copies a battle actor's value word and facing into a script-visible result (src/BATTLE/BATTLE.PRG/3A1A0.c).
const psx = @import("common").psx;
const v = @import("views_3A1A0");

/// orig: func_800A36E0 (src/BATTLE/BATTLE.PRG/3A1A0.c)
/// Writes actor `arg0`'s value word and facing (in 16ths) to `arg2`, taking the tile position for modes 0x10 and 0x20.
pub fn func_800A36E0(arg0: c_int, arg1: c_int, arg2: ?*v.func_8006EBF8_t) callconv(.c) void {
    const temp = psx.at(v.g.D_800F4538(), arg0).*;
    if (temp) |t| {
        const out = arg2.?;
        out.unk0.unk0.value = t.unk1868;
        if (psx.getBits(&t.unk0, "unkA_5") != 0 and (arg1 == 0x10 or arg1 == 0x20)) {
            // *(int*)&temp->unk0.currentTileX: an unaligned 4-byte read across the tile fields.
            const word: *align(1) i32 = @ptrCast(&t.unk0.currentTileX);
            out.unk0.unk0.value = word.*;
        }
        psx.setBits(&out.unk0.unk0.fields, "unk0_24", psx.conv(u8, @divTrunc(@as(c_int, t.unk0.facing), 16)));
    }
}
