//! Starts a command's animation on battle actor slot `slot` (src/BATTLE/BATTLE.PRG/30DB0.c).
const psx = @import("common").psx;
const v = @import("views_30DB0");
const c = @import("c_30DB0");

/// orig: func_8009DF3C (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Starts the animation for command `command` (0..3) on actor `slot`, and copies its pose when the command is not 3.
pub fn func_8009DF3C(slot: c_int, command: c_int) callconv(.c) void {
    // The C does not check slot against the 17 entries: psx.at keeps its unchecked index.
    const temp: *v.D_800F4538_t = psx.at(v.g.D_800F4538(), slot).*.?;
    // Uninitialised in the original; the callee writes sp10[0].
    var sp10: [2][*c]c_ushort = .{ null, null };

    var var_s0: c_int = 0;
    var var_s1: c_int = 0;

    if (temp.unk0.unkF == 0) {
        return;
    }

    switch (command) {
        0 => {
            var_s0 = 6;
        },
        1 => {
            var_s0 = 0xC6;
        },
        2 => {
            var_s0 = 0xC8;
        },
        3 => {
            var_s0 = @as(c_int, temp.animationId);
            if (var_s0 != 0xC6 and var_s0 != 0xC8) {
                return;
            }
            var_s0 +%= 1;
            var_s1 = 8;
            _ = c.func_800AD494(@ptrCast(temp), var_s0, &sp10);
            if (sp10[0] == null) {
                var_s0 = 6;
                var_s1 = 0x10;
            }
        },
        else => {},
    }

    _ = c.func_800A0204(slot, var_s0, 0, var_s1);
    if (command < 3) {
        c.vs_main_memcpy(&temp.unk704, &temp.unkC54, @as(c_uint, @sizeOf(v.D_800F4538_unkC54)));
    }
}
