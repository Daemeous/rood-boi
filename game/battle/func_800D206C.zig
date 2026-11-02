//! Steps the camera shakes (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800D206C (src/BATTLE/BATTLE.PRG/5BF94.c)
/// For each of the four active shakes (D_800F5518's bits): its amplitude from func_800D11F4 times a sine into the camera's unk20..unk2C, its phase advanced; a finished one (negative) is stopped (func_800D1FEC).
pub fn func_800D206C() callconv(.c) void {
    const shakes = v.g.D_800F54B8();
    const camera = v.g.D_800F54D8();
    var i: u5 = 0;
    while (i < 4) : (i += 1) { // i < 4
        if (((@as(c_int, v.g.D_800F5518().*) >> i) & 1) == 0) {
            continue;
        }
        const shake = &shakes[i];
        var amount = c.func_800D11F4(psx.getBits(shake, "unk1_0"), shake.unk0);
        if (amount >= 0) {
            amount = ((c.rsin(@as(c_int, shake.unk5) * 32) *% amount) *% shake.unk4) >> 8;
            switch (i) {
                0 => camera.unk20 = amount,
                1 => camera.unk24 = amount,
                2 => camera.unk28 = amount,
                3 => camera.unk2C = amount >> 12,
                else => unreachable,
            }
            // Object order: func_800D1E2C, then func_800D1E78.
            const first = c.func_800D1E2C(@ptrCast(shake));
            const second = c.func_800D1E78(@ptrCast(shake));
            shake.unk5 = psx.conv(u8, @as(c_int, shake.unk5) + first + second);
            shake.unk0 +%= 1;
        } else {
            c.func_800D1FEC(@as(c_int, 1) << i);
        }
    }
}
