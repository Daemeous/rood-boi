//! Builds a 3-halfword record from a function-local allocation and bumps the
//! global record counter (src/BATTLE/BATTLE.PRG/2842C.c).
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: func_80093B04 (src/BATTLE/BATTLE.PRG/2842C.c)
/// Fills a record from the three halfwords at `words`, tags it as kind 3, and counts it.
pub fn func_80093B04(words: [*c]c_ushort) callconv(.c) c_int {
    const rec: *align(1) v.func_800962E4_t = @ptrCast(c.func_800962E4().?);
    rec.unkB = 0;
    rec.unk8 = 3;
    rec.unk0 = words[0];
    rec.unk2 = words[1];
    rec.unk4 = words[2];
    v.g.D_800F227E().* +%= 1;
    return 0;
}
