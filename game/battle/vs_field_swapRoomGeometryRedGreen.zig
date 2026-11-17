//! Swaps paired bytes in the two entry tables of a room section block (pc/src/game/BATTLE/BATTLE.PRG/2842C.c).
const psx = @import("common").psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: vs_field_swapRoomGeometryRedGreen (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Swaps the byte pairs of the two tables in the room section block that `vs_field_findRoomGeometryGroup(arg0)` locates.
pub fn vs_field_swapRoomGeometryRedGreen(arg0: c_int) callconv(.c) void {
    // C reads the base before calling vs_field_findRoomGeometryGroup; the base is a guest address, used as integer arithmetic.
    const base_addr: u32 = psx.rawOf(v.g.vs_battle_roomData().geometrySection);
    const entry_opt = c.vs_field_findRoomGeometryGroup(arg0);
    if (entry_opt == null) return;
    // vs_field_findRoomGeometryGroup returns an int*: the entry is read as a guest int array.
    const entry: [*]align(1) c_int = @ptrCast(entry_opt.?);
    const block_addr: u32 = base_addr +% @as(u32, @bitCast(entry[7]));
    const block: [*]align(1) c_int = @ptrFromInt(@as(usize, block_addr));
    const n0 = block[0];
    const n1 = block[1];
    var t: [*]v.vs_field_MpdTriangleColorView = @ptrCast(block + 2);
    var i: c_int = 0;
    while (i < n0) : (i +%= 1) {
        const tmp0 = t[0].red1;
        t[0].red1 = t[0].green1;
        t[0].green1 = tmp0;
        const tmp1 = t[0].red2;
        t[0].red2 = t[0].green2;
        t[0].green2 = tmp1;
        const tmp2 = t[0].red3;
        t[0].red3 = t[0].green3;
        t[0].green3 = tmp2;
        t += 1;
    }
    var q: [*]v.vs_field_MpdQuadColorView = @ptrCast(t);
    i = 0;
    while (i < n1) : (i +%= 1) {
        const tmp0 = q[0].red1;
        q[0].red1 = q[0].green1;
        q[0].green1 = tmp0;
        const tmp1 = q[0].red2;
        q[0].red2 = q[0].green2;
        q[0].green2 = tmp1;
        const tmp2 = q[0].red3;
        q[0].red3 = q[0].green3;
        q[0].green3 = tmp2;
        const tmp3 = q[0].red4;
        q[0].red4 = q[0].green4;
        q[0].green4 = tmp3;
        q += 1;
    }
}
