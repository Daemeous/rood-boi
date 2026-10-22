//! Interpolates a block-9 vector and a curve value from the file's block 9, then passes both on
//! (src/BATTLE/BATTLE.PRG/6F014.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_6F014");
const c = @import("c_6F014");

/// orig: func_800D78F0 (src/BATTLE/BATTLE.PRG/6F014.c)
/// Lerps a block-9 vector and computes a curve value from block 9, then hands both to the next steps.
pub fn func_800D78F0() callconv(.c) void {
    var sp10: v.VECTOR = undefined;
    var sp20: c_int = undefined;

    const block9 = psx.get(v.g.D_800F569C().*.?.block9Data).?;
    c.vs_battle_lerpVector(@ptrCast(&block9.unk0), v.g.D_800F5330()[block9.unk12], @ptrCast(&sp10));
    sp20 = c.func_800CFE1C(@ptrCast(&block9.unkC), v.g.D_800F5330()[block9.unk13]);
    c.func_800D5260(@ptrCast(&sp10));
    c.func_800D5294(@ptrCast(&sp20));
}
