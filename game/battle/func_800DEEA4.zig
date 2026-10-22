//! Unit: func_800DEEA4 from src/BATTLE/BATTLE.PRG/74088.c.
const common = @import("common");
const psx = common.psx;
const v = @import("views_74088");
const c = @import("c_74088");

/// orig: func_800DEEA4 (src/BATTLE/BATTLE.PRG/74088.c)
/// Clears the entry flag at 0x9A of the entry this actor links to (when it has no link value), then calls func_800DC810 on the actor.
pub fn func_800DEEA4(arg0: ?*v.func_800DEEA4_t) callconv(.c) void {
    if (arg0) |actor| {
        if (actor.unk44 == 0) {
            // Unchecked as in the C (unk4 past the 18 entries reads on).
            const entry = psx.at(v.g.D_800F5878(), actor.unk4).*;
            if (entry) |e| {
                e.unk9A = 0;
            }
        }

        c.func_800DC810(psx.cptr(c.func_800DEEA4_t, actor));
    }
}
