```zig
//! Status 0x100000 on the character (src/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: func_8006B214 (src/BATTLE/BATTLE.PRG/146C.c)
/// Clears status 0x100000 from the character when it is set.
pub fn func_8006B214() callconv(.c) void {
    const stats = psx.get(v.g.vs_battle_characterState().*.?.stats).?;
    if (stats.statuses & 0x100000 != 0) {
        c.func_80093FEC(stats.unk957, 0, 0x100000, 1);
        c.func_80086754(0x100000, @ptrCast(stats));
    }
}
```
