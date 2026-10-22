//! Derives a threshold from the table entry matching an id, and stores it in arg0's bitfield (src/BATTLE/BATTLE.PRG/74088.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_74088");

/// orig: func_800DCBD8 (src/BATTLE/BATTLE.PRG/74088.c)
/// Takes the threshold of the first entry with id unkC_0 and no kill flag, and stores it scaled by 1.25 in unkC_19.
pub fn func_800DCBD8(arg0: ?*v.func_800DCAA0_t) callconv(.c) void {
    const a = arg0.?;
    var i: c_int = 0;
    var threshold: c_int = 0;
    const id: c_int = @as(c_int, psx.getBits(a, "unkC_0"));
    const table = v.g.D_800F5900().*.?;

    while (i < @as(c_int, table.unk4A)) : (i +%= 1) {
        const entry = psx.at(&table.unk4C, i);
        if (entry.unk40 == 0 and id == @as(c_int, entry.unk0)) {
            threshold = @as(c_int, entry.unk2);
            break;
        }
    }

    const scaled: c_int = threshold +% (threshold >> 2) +% (threshold >> 5);
    psx.setBits(a, "unkC_19", psx.conv(u8, scaled));
}
