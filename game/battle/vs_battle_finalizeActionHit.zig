//! Battle state byte machine: settles a 2-bit-per-step flag word into a
//! state byte (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");

/// orig: vs_battle_finalizeActionHit (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Advances the state byte at unk3 from the flag word at unk1C, or from unk14/unk18 when that word is zero.
pub fn vs_battle_finalizeActionHit(arg0: ?*v.func_80085A34_t) callconv(.c) void {
    const state = arg0.?;
    if (state.unk40 == 0) {
        if (state.unk2 == 0xFF) {
            state.unk2 = 0;
        }
        if (state.unk3 == 6) {
            var bits: c_uint = psx.conv(c_uint, state.unk1C);
            if (bits != 0) {
                // Nine 2-bit digits, lowest first; digit 0 leaves the state alone.
                var i: c_int = 0;
                while (i < 9) : ({
                    bits /= 4;
                    i += 1;
                }) {
                    switch (bits & 3) {
                        2 => {
                            state.unk3 = 5;
                            return;
                        },
                        1 => {
                            state.unk3 = 0;
                            return;
                        },
                        3 => {
                            state.unk3 = 1;
                            return;
                        },
                        else => {},
                    }
                }
            } else if ((state.unk14 | state.unk18) == 0) {
                state.unk3 = 4;
                state.unk2 = 0;
            }
        }
    }
}
