//! Tests whether a point offset from arg0's transform lies within a radius, by its transformed squared length (pc/src/game/BATTLE/BATTLE.PRG/58578.c).
const psx = @import("common").psx;
const gte = @import("common").gte;
const v = @import("views_58578");
const c = @import("c_58578");

/// orig: vs_battle_isPointInTargetEllipsoid (pc/src/game/BATTLE/BATTLE.PRG/58578.c)
/// Returns 1 when the transformed offset's squared length, scaled by 2^16, is under 1.
pub fn vs_battle_isPointInTargetEllipsoid(arg0: ?*v.func_800C1564_t, arg1: [*c]c_ushort) callconv(.c) c_int {
    var work: v.func_800C0FA8_t2 = undefined;
    var matrix: v.MATRIX = undefined;
    var sum: c_int = 0;

    _ = c.vs_battle_buildTargetVolumeTestTransform(@ptrCast(arg0), @ptrCast(&work), @ptrCast(&matrix));

    // work.unk0 = arg1[k] - work.unk10[k], stored as shorts.
    {
        const dst: *[3]i16 = @ptrCast(&work.unk0);
        var k: usize = 0;
        while (k < 3) : (k += 1) {
            const pos: c_int = @as(c_int, arg1[k]);
            const origin: c_int = @as(c_int, @as(u16, @bitCast(work.unk10[k])));
            dst[k] = psx.conv(i16, pos -% origin);
        }
    }

    gte.applyMatrixSV(&work.unk8, &matrix, &work.unk0);

    {
        const src: *[3]i16 = @ptrCast(&work.unk8);
        var i: usize = 0;
        while (i < 3) : (i += 1) {
            const term: c_int = (@as(c_int, src[i]) *% @as(c_int, work.unk18[i])) >> 12;
            sum +%= term *% term;
        }
    }

    return @as(c_int, @intFromBool(psx.conv(c_uint, sum >> 16) < 1));
}
