//! Swaps paired bytes in the two entry tables of a room section block (src/BATTLE/BATTLE.PRG/2842C.c).
const psx = @import("common").psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: func_80093914 (src/BATTLE/BATTLE.PRG/2842C.c)
/// Swaps the byte pairs of the two tables in the room section block that `func_8009195C(arg0)` locates.
pub fn func_80093914(arg0: c_int) callconv(.c) void {
    // C reads the base before calling func_8009195C; the base is a guest address, used as integer arithmetic.
    const base_addr: u32 = psx.rawOf(v.g.vs_battle_roomData().geometrySection);
    const entry_opt = c.func_8009195C(arg0);
    if (entry_opt == null) return;
    // func_8009195C returns an int*: the entry is read as a guest int array.
    const entry: [*]align(1) c_int = @ptrCast(entry_opt.?);
    const block_addr: u32 = base_addr +% @as(u32, @bitCast(entry[7]));
    const block: [*]align(1) c_int = @ptrFromInt(@as(usize, block_addr));
    const n0 = block[0];
    const n1 = block[1];
    var t: [*]v.func_80093914_t = @ptrCast(block + 2);
    var i: c_int = 0;
    while (i < n0) : (i +%= 1) {
        const tmp0 = t[0].unkC;
        t[0].unkC = t[0].unkD;
        t[0].unkD = tmp0;
        const tmp1 = t[0].unk10;
        t[0].unk10 = t[0].unk11;
        t[0].unk11 = tmp1;
        const tmp2 = t[0].unk14;
        t[0].unk14 = t[0].unk15;
        t[0].unk15 = tmp2;
        t += 1;
    }
    var q: [*]v.func_80093914_t2 = @ptrCast(t);
    i = 0;
    while (i < n1) : (i +%= 1) {
        const tmp0 = q[0].unkC;
        q[0].unkC = q[0].unkD;
        q[0].unkD = tmp0;
        const tmp1 = q[0].unk10;
        q[0].unk10 = q[0].unk11;
        q[0].unk11 = tmp1;
        const tmp2 = q[0].unk14;
        q[0].unk14 = q[0].unk15;
        q[0].unk15 = tmp2;
        const tmp3 = q[0].unk24;
        q[0].unk24 = q[0].unk25;
        q[0].unk25 = tmp3;
        q += 1;
    }
}
