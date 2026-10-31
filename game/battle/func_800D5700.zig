//! Copies two packed fields from a table entry into a state record (src/BATTLE/BATTLE.PRG/5BF94.c).
const psx = @import("common").psx;
const v = @import("views_5BF94");

/// orig: func_800D5700 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Loads unkC and unkA from the table entry at index unk6 of the table at unk0.
pub fn func_800D5700(arg0: ?*v.func_800D5780_t) callconv(.c) void {
    const a = arg0.?;
    const base: [*]v.func_800D57FC_t = @ptrCast(psx.get(a.unk0).?);
    // The index is re-read from a.unk6 for each store, as the C reads arg0->unk6 each time.
    a.unkC = psx.conv(i32, psx.getBits(psx.at(base, a.unk6), "unk0_20"));
    a.unkA = psx.conv(i16, psx.getBits(psx.at(base, a.unk6), "unk0_8"));
}
