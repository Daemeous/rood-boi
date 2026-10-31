//! Battle helper in 5BF94.c: runs func_800D5198 on the block and passes its result on to func_800D6AEC.
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800D4E5C (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Calls func_800D6AEC on the block with the value func_800D5198 returns for it; always 1.
pub fn func_800D4E5C(arg0: ?*v.D_800F53B8_t) callconv(.c) c_int {
    const r = c.func_800D5198(@ptrCast(arg0));
    c.func_800D6AEC(@ptrCast(arg0), psx.conv(c_int, r));
    return 1;
}
