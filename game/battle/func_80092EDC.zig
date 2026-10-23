//! Copies a record's position (scaled down by 16) and vector into the block its
//! pointer field names (src/BATTLE/BATTLE.PRG/2842C.c).
const psx = @import("common").psx;
const v = @import("views_2842C");

/// orig: func_80092EDC (src/BATTLE/BATTLE.PRG/2842C.c)
/// When the record's flag is non-negative, writes its position (divided by 16) and its vector into the block it points at.
pub fn func_80092EDC(arg0: ?*v.func_80092F74_t) callconv(.c) void {
    const rec = arg0.?;
    if (rec.unk8 >= 0) {
        const temp_v1: [*]i32 = @ptrCast(psx.get(rec.unk0).?);
        temp_v1[1] = @divTrunc(rec.unkC, 16);
        temp_v1[2] = @divTrunc(rec.unk10, 16);
        temp_v1[3] = @divTrunc(rec.unk14, 16);

        temp_v1[4] = rec.unk1C.vx;
        temp_v1[5] = rec.unk1C.vy;
        temp_v1[6] = rec.unk1C.vz;
    }
}
