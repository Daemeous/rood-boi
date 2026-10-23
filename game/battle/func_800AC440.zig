//! Assigns or releases one of the two armature slots of a battle actor (src/BATTLE/BATTLE.PRG/40564.c).
const psx = @import("common").psx;
const v = @import("views_40564");
const c = @import("c_40564");

/// orig: func_800AC440 (src/BATTLE/BATTLE.PRG/40564.c)
/// Claims a free one of the two armature slots for armature `arg1` (`arg2 != 0`), or releases it (`arg2 == 0`).
pub fn func_800AC440(arg0: ?*v.D_800F4538_t, arg1: c_int, arg2: c_int) callconv(.c) void {
    const self = arg0.?;

    // &psx_get(unk68)->armatures[arg1]: an address only, the C dereferences it when it writes.
    // A null base stays 0, so the offset is the address the C computes.
    const base_addr: usize = if (psx.get(self.unk0.unk68)) |p| @intFromPtr(p) else 0;
    const off: isize = @as(isize, @offsetOf(v.D_800F4538_unk68, "armatures")) +% @as(isize, arg1) *% @sizeOf(v._armature_t);
    const temp_s2: *align(1) v._armature_t = @ptrFromInt(base_addr +% @as(usize, @bitCast(off)));

    var i: c_int = 0;
    while (i < 2) : (i +%= 1) {
        const idx: usize = @intCast(i);
        if (arg2 == 0) {
            if (@as(c_int, self.unk18D0[idx]) == arg1) {
                self.unk18D0[idx] = 0;
                temp_s2.unkC = 0;
                return;
            }
        } else if (self.unk18D0[idx] == 0) {
            var sp10: c_int = undefined;
            c.func_800A1720(@as(c_int, self.unk0.unkF), arg1, &self.unk1890[idx], &sp10);
            temp_s2.unkC = psx.conv(u8, i +% 1);
            self.unk18D0[idx] = psx.conv(u8, arg1);
            return;
        }
    }
}
