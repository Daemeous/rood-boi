//! Retires a slot of the D_800F1DD8 table, freeing the block it holds (src/BATTLE/BATTLE.PRG/2842C.c).
const psx = @import("common").psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: func_8009291C (src/BATTLE/BATTLE.PRG/2842C.c)
/// Marks table slot `arg0` in use; for slots from 64 up to the limit, frees the held block and takes over the spare one.
pub fn func_8009291C(arg0: c_int) callconv(.c) c_int {
    if (arg0 < @as(c_int, v.g.D_800F225A().*)) {
        const entry = psx.at(v.g.D_800F1DD8(), arg0);
        entry.unk0 = 1;
        return 1;
    }
    if (arg0 < 0x40) {
        return 0;
    }
    if (arg0 < @as(c_int, v.g.D_800F225B().*) +% 0x40) {
        const entry = psx.at(v.g.D_800F1DD8(), arg0);
        if (entry.unk0 != 0 or psx.get(entry.unk4) != psx.get(entry.unk8)) {
            c.vs_main_freeHeap(psx.get(entry.unk4));
        }
        entry.unk0 = 1;
        psx.put(&entry.unk4, psx.get(entry.unk8));
        c.func_800927AC(psx.cptr(c.D_800F1DD8_t, entry));
        return 1;
    }
    return 0;
}
