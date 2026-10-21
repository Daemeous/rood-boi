//! Applies a battle actor's pending step: moves it to its next tile if the ground allows, else falls or settles its height (src/BATTLE/BATTLE.PRG/38C1C.c).
const psx = @import("common").psx;
const v = @import("views_38C1C");
const c = @import("c_38C1C");

/// orig: func_800A25EC (src/BATTLE/BATTLE.PRG/38C1C.c)
/// Moves an actor by its pending step if the ground allows, otherwise drops it or settles its height.
pub fn func_800A25EC(actor: ?*v.D_800F4538_t) callconv(.c) void {
    const a = actor.?;
    psx.setBits(&a.unk0, "unkA_5", 0);

    var next: v.SVECTOR = undefined;
    next.vx = a.unk1848.unk10.vx;
    next.vy = 0;
    next.vz = a.unk1848.unk10.vz;
    next.vy = psx.conv(i16, c.func_800A6EE8(
        psx.cptr(c.SVECTOR, &a.unk0.position),
        @as(c_int, next.vx),
        @as(c_int, next.vz),
        1,
    ));
    if (@as(c_int, next.vy) == -3000) return;

    next.vx = psx.conv(i16, @as(c_int, next.vx) +% @as(c_int, a.unk0.position.vx));
    next.vz = psx.conv(i16, @as(c_int, next.vz) +% @as(c_int, a.unk0.position.vz));
    if (c.func_800A8D64(psx.cptr(c.SVECTOR, &next), 0) == null) return;

    a.unk0.position.vx = next.vx;
    a.unk0.position.vz = next.vz;

    if ((psx.byteAt(c_uint, a, 8).* & 0x70000) != 0) {
        c.func_800AC690(@as(c_int, a.unk0.unkF), @ptrCast(a));
    } else {
        const height: i16 = next.vy;
        if (@as(c_int, height) > @as(c_int, a.unk0.position.vy) and
            @as(c_int, height) -% @as(c_int, a.unk0.position.vy) >= 64)
        {
            a.unk0.unk34.vx = 0;
            a.unk0.unk34.vy = 0;
            a.unk0.unk34.vz = 0;
            c.func_800A0204(@as(c_int, a.unk0.unkF), 47, 0, 4);
            a.unk181A = 0;
            psx.setBits(&a.unk0, "unkA_3", 0);
            psx.setBits(&a.unk0, "unk9_6", 0);
            psx.setBits(&a.unk0, "unkA_0", 3);
        } else {
            a.unk0.position.vy = height;
        }
    }

    // Tile index: signed division by 128, as the C divides the int position.
    a.unk0.currentTileX = psx.conv(u8, @divTrunc(@as(c_int, a.unk0.position.vx), 128));
    a.unk0.currentTileZ = psx.conv(u8, @divTrunc(@as(c_int, a.unk0.position.vz), 128));
    a.unk0.unk5D = 0;
}
