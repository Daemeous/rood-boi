//! The room point's lock message, or its chest (src/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: func_800743E0 (src/BATTLE/BATTLE.PRG/146C.c)
/// Camera mode 3; a point with a key or sigil shows its message (1 -> 2, 2 -> 1, 3 -> 3) and calls func_8006C2FC;
/// else its unk10 below 0x40 hands the loot after it to func_800CB18C, from 0x40 up the item unk10 - 0x40 to
/// func_800CB1C0, then func_8006C39C.
pub fn func_800743E0() callconv(.c) void {
    const kind: c_int = psx.get(v.g.D_800F1910().unk0).?.keyOrSigil;
    v.g._cameraMode().* = 3;
    if (kind != 0) {
        // Uninitialised in the original when kind is not 1..3 (no default case).
        var message: c_int = 0;
        switch (kind) {
            1 => message = 2,
            2 => message = 1,
            3 => message = 3,
            else => {},
        }
        c.vs_battle_displaySceneMessage(message, psx.get(v.g.D_800F1910().unk0).?.unkE, 1);
        c.func_8006C2FC();
    } else {
        const item = psx.get(v.g.D_800F1910().unk0).?.unk10;
        if (item < 0x40) {
            c.func_800CB18C(@ptrCast(psx.at(psx.get(v.g.D_800F1910().unk0).?, 1)));
        } else {
            c.func_800CB1C0(@as(c_int, item) -% 0x40);
        }
        c.func_8006C39C();
    }
}
