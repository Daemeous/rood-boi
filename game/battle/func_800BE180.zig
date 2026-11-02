//! Builds the camera shake vectors from the two shake entries and publishes
//! the result in the camera block (src/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800BE180 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Sums the active camera shake entries into the scratchpad vectors and publishes the result in the camera block.
pub fn func_800BE180() callconv(.c) void {
    const shake: *v.cameraShakeScratch = @ptrFromInt(0x1F800088);
    shake.b.vz = 0;
    shake.b.vy = 0;
    shake.b.vx = 0;
    shake.a.vz = 0;
    shake.a.vy = 0;
    shake.a.vx = 0;

    const base: *v.D_800F4BA4_t = v.g.D_800F4BA4().*.?;

    var count: i16 = 0;
    var i: i16 = 0;
    var entry: *v.func_800BDF6C_t = @ptrCast(&base.unk1A4.unk2);

    while (i < 2) : ({
        entry = @ptrFromInt(@intFromPtr(entry) +% @sizeOf(v.func_800BDF6C_t));
        i +%= 1;
    }) {
        if (c.func_800BE01C(psx.cptr(c.func_800BDF6C_t, entry)) != 0) {
            const unkA: c_int = entry.unkA;
            const x: c_int = @as(c_int, entry.unk2) *% (((unkA >> 2) & 3) -% 1);
            const y: c_int = @as(c_int, entry.unk2) *% (((unkA >> 4) & 3) -% 1);
            const z: c_int = @as(c_int, entry.unk2) *% ((unkA >> 6) -% 1);

            shake.c.vx = psx.conv(i16, x);
            shake.c.vy = psx.conv(i16, y);
            shake.c.vz = psx.conv(i16, z);

            if ((unkA & 1) != 0) {
                shake.a.vx = psx.conv(i16, @as(c_int, shake.a.vx) +% x);
                shake.a.vy = psx.conv(i16, @as(c_int, shake.a.vy) +% y);
                shake.a.vz = psx.conv(i16, @as(c_int, shake.a.vz) +% z);
            }
            if ((unkA & 2) != 0) {
                shake.b.vx = psx.conv(i16, @as(c_int, shake.b.vx) +% @as(c_int, shake.c.vx));
                shake.b.vy = psx.conv(i16, @as(c_int, shake.b.vy) +% @as(c_int, shake.c.vy));
                shake.b.vz = psx.conv(i16, @as(c_int, shake.b.vz) +% @as(c_int, shake.c.vz));
            }
            count +%= 1;
        }
    }

    base.unk1A4.unk0.s16 = count;
    if (count != 0) {
        const a: *v.SVECTOR = @ptrFromInt(0x1F8000D8);
        const b: *v.SVECTOR = @ptrFromInt(0x1F8000E0);
        base.unk1BE = a.vx;
        base.unk1C0 = a.vy;
        base.unk1C2 = a.vz;
        base.unk1C6 = b.vx;
        base.unk1C8 = b.vy;
        base.unk1CA = b.vz;
    }
}
