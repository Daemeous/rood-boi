//! Moves a battle actor by its velocity, updates its tile, and selects its movement animation (src/BATTLE/BATTLE.PRG/38C1C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_38C1C");
const c = @import("c_38C1C");

/// orig: func_800A2790 (src/BATTLE/BATTLE.PRG/38C1C.c)
/// Advances an actor by its velocity, sets its flag bit 21, and picks its movement animation.
pub fn func_800A2790(actor: ?*v.D_800F4538_t) callconv(.c) void {
    const a = actor.?;
    const speed: c_int = psx.conv(c_int, a.unk1848.unk8);

    const dvx: c_int = @divTrunc(@as(c_int, a.unk1848.unk10.vx) *% speed, 0x1000000);
    const dvy: c_int = @divTrunc(@as(c_int, a.unk1848.unk10.vy) *% speed, 0x1000000);
    const dvz: c_int = @divTrunc(@as(c_int, a.unk1848.unk10.vz) *% speed, 0x1000000);

    a.unk0.position.vx = psx.conv(i16, @as(c_int, a.unk0.position.vx) +% dvx);
    a.unk0.position.vy = psx.conv(i16, @as(c_int, a.unk0.position.vy) +% dvy);
    a.unk0.position.vz = psx.conv(i16, @as(c_int, a.unk0.position.vz) +% dvz);

    a.unk0.currentTileX = psx.conv(u8, @divTrunc(@as(c_int, a.unk0.position.vx), 128));
    a.unk0.currentTileZ = psx.conv(u8, @divTrunc(@as(c_int, a.unk0.position.vz), 128));

    // *(u_int*)((char*)actor + 8) |= 0x200000: a 32-bit access at byte 8, unaligned as the C has it.
    const flags = psx.byteAt(u32, a, 8);
    flags.* |= 0x200000;

    c.func_800AA850(a.unk0.unkF, a.unk1848.unk6, 12);

    var animation: c_int = 1;
    if (speed > 0) {
        if (dvy < -2) {
            animation = 32;
        } else {
            animation = 31;
            if (dvy >= 3) animation = 34;
        }
        if (speed >= 12) animation +%= 6;
    }

    if (@as(c_int, a.animationId) != animation) {
        c.func_800A0204(a.unk0.unkF, animation, 0, 4);
    }
}
