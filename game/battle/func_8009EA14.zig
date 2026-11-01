//! Plays an actor's reaction animation, picked from where the source of the hit stands (src/BATTLE/BATTLE.PRG/30DB0.c).
const psx = @import("common").psx;
const v = @import("views_30DB0");
const c = @import("c_30DB0");

/// orig: func_8009EA14 (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Picks actor `actorId`'s reaction to a hit from `source` (null: none): 0x40/0x42 front or back, for the
/// player 0x44/0x45 by elevation, 0x5B when unk5AC_3 is set, 0x3B in state unkA_3 2; resets its motion state
/// and starts the animation.
pub fn func_8009EA14(actorId: c_int, source: ?*v.SVECTOR) callconv(.c) void {
    const actor = psx.at(v.g.D_800F4538(), actorId).* orelse return;

    var animation: c_int = 0x5B;
    if (psx.getBits(actor, "unk5AC_3") == 0) {
        side: {
            if (actorId == 0) {
                var elevation: c_int = 0;
                if (source) |s| {
                    var dx: c_int = @as(c_int, s.vx) -% actor.unk0.position.vx;
                    dx *%= dx;
                    const dz: c_int = @as(c_int, s.vz) -% actor.unk0.position.vz;
                    if (dx < 0) dx = -%dx;
                    var dz2: c_int = dz *% dz;
                    if (dz2 < 0) dz2 = -%dz2;
                    const distance = c.vs_gte_rsqrt(dx +% dz2);
                    var anchor: v.SVECTOR = undefined;
                    c.func_800A1AF8(actor.unk0.unkF, 0, psx.cptr(c.SVECTOR, &anchor), 0);
                    elevation = c.ratan2(@as(c_int, s.vy) -% anchor.vy, distance);
                }
                animation = 0x45;
                if (elevation < -0xE4) break :side;
                animation = 0x44;
                if (elevation >= 0xAC) break :side;
            }
            // block_11
            var angle: c_int = 0;
            if (source) |s| {
                const dx: c_int = @as(c_int, s.vx) -% actor.unk0.position.vx;
                const dz: c_int = @as(c_int, s.vz) -% actor.unk0.position.vz;
                if (dx == 0 and dz == 0) {
                    angle = actor.unk0.facing;
                } else {
                    angle = (0xC00 -% c.ratan2(dz, dx)) & 0xFFF;
                }
                angle -%= actor.unk0.facing;
                if (angle > 0x800) {
                    angle -%= 0x1000;
                } else if (angle < -0x800) {
                    angle +%= 0x1000;
                }
            }
            const turned: c_uint = psx.conv(c_uint, angle +% 0x200);
            animation = 0x40;
            if (turned >= 0x801) animation = 0x42;
        }
    }

    // var_s4 is uninitialised in the original on the unkA_3 == 2 path (a caller's register): 0 here.
    var blend: c_int = 0;
    if (psx.getBits(&actor.unk0, "unkA_3") == 2) {
        const pending = psx.getBits(&actor.unk0, "unkC_0");
        if (pending != 0) {
            c.func_8008C49C(@as(c_int, pending) -% 2, -1);
            psx.setBits(&actor.unk0, "unkC_0", 0);
        }
        animation = 0x3B;
    } else {
        if (psx.getBits(&actor.unk0, "unkB_0") == 3) c.func_800A2C48(@ptrCast(actor));
        actor.unk0.unk18 = 0;
        if (actor.unk0.unkF == 0) {
            actor.unk0.unk11 = 0;
            psx.setBits(&actor.unk0, "unkA_3", 0);
            psx.setBits(&actor.unk0, "unk9_6", 0);
            psx.setBits(&actor.unk0, "unkA_0", 0);
            blend = 0;
        } else {
            blend = 8;
        }
    }
    c.func_800A525C(@ptrCast(actor));
    actor.unk6E3 = 0xFF;
    _ = c.func_800A0204(actorId, animation, 0, blend);
    c.func_8009FD38(@ptrCast(actor));
}
