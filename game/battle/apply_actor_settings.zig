//! Fills an actor's settings state from its settings word and its actor's flags
//! (src/BATTLE/BATTLE.PRG/7CD68.c).
const psx = @import("common").psx;
const v = @import("views_7CD68");
const c = @import("c_7CD68");

// The unit's own prototype names actionMetadataState, whose table is opaque in
// the c module; the call passes the same pointer, so the untyped pointer is the
// same ABI without pulling that declaration into this object.
extern fn func_800E5710(state: ?*anyopaque) callconv(.c) void;

/// orig: func_800E5A9C (src/BATTLE/BATTLE.PRG/7CD68.c)
/// Fills an actor's settings state from its settings word and its actor's flags.
pub fn applyActorSettings(state: ?*v.actorSettingsState, settings: ?*v.actorSettingsWord) callconv(.c) void {
    const s = state.?;
    const w = settings.?;

    var rangeSquared: c_uint = v.g.D_80069C1C()[@as(usize, (w.half[1] & 28) >> 2)];
    s.rangeSquared = rangeSquared;
    if (rangeSquared > 0x23FFFF) rangeSquared = 0x240000;
    s.limit = rangeSquared;
    s.angle = v.g.D_800F17B8()[@as(usize, w.half[1] & 3)][0];
    const halfAngle: c_int = @divTrunc(@as(c_int, s.angle), 2);
    s.cosine = c.rcos(@divTrunc(halfAngle *% 4096, 360));
    s.f25 = psx.conv(u8, (w.raw >> 15) & 1);
    s.f29 = psx.conv(u8, (w.raw >> 14) & 1);
    s.f24 = psx.conv(u8, (w.raw >> 13) & 1);
    s.f2D = 0;
    if (v.g.D_800F58C0().* != 0) {
        const table = v.g.D_800F58C8().*.?;
        var i: c_int = 0;
        while (i < 8) : (i += 1) {
            if (table[@intCast(i)] >> 15 != 0) break;
        }
        if (i != 8) s.f2D = psx.conv(u8, (w.raw >> 12) & 1);
    }
    s.fB2 = psx.conv(u8, ((w.raw >> 10) & 3) +% 1);
    s.fB0 = v.g.D_800F17C8()[@as(usize, (w.raw >> 8) & 3)][0];
    var step: c_uint = 128;
    if (s.collisionRadius > 128) step = 32;
    s.fBA = psx.conv(u16, (step *% @as(c_uint, v.g.D_800F17D8()[@as(usize, (w.raw >> 2) & 3)])) >> 4);
    s.f23 = 1;
    s.f21 = w.byte[3] & 1;
    s.f22 = psx.conv(u8, (w.raw >> 23) & 1);
    s.f2C = psx.conv(u8, (w.raw >> 21) & 3);
    s.f2F = psx.conv(u8, (w.raw >> 28) & 3);
    s.f31 = psx.conv(u8, w.raw >> 30);
    s.f20 = psx.conv(u8, (w.raw >> 26) & 1);
    s.fD6 = v.g.D_80069C3C()[@as(usize, (w.raw >> 4) & 7)].first;
    s.fD4 = v.g.D_80069C3C()[@as(usize, (w.raw >> 4) & 7)].second;
    s.fD8 = v.g.D_80069C3C()[@as(usize, (w.raw >> 4) & 7)].third;
    s.f27 = psx.conv(u8, (w.raw >> 27) & 1);
    s.f30 = psx.conv(u8, (w.raw >> 25) & 1);
    if ((s.flags & 0xC0) == 0) s.f32 = v.g.D_80069C18()[@as(usize, s.f31)];
    s.f33 = w.byte[0] >> 7;

    const actor = psx.get(s.actor).?;
    const mode: c_int = @as(c_int, psx.getBits(&actor.flags34.bits, "mode"));
    s.f26 = psx.conv(u8, mode >> 1);
    s.f2B = psx.conv(u8, mode & 1);
    s.f102 = v.g.D_80069C08()[@as(usize, (actor.flags34.raw >> 14) & 1)];
    s.f101 = psx.conv(u8, (actor.flags.raw >> 14) & 3);
    s.fC8 = actor.flags.half[1] & 63;
    s.fCC = psx.conv(u8, (actor.flags.raw >> 22) & 31);
    s.fCE = psx.conv(u8, actor.flags.raw >> 27);
    if (psx.conv(c_uint, @as(c_int, actor.kind) -% 0xAC) < 2) {
        const handler: ?*anyopaque = @ptrCast(@constCast(&c.func_800E7F8C));
        psx.put(&s.callback, handler);
        s.f19 = 1;
        c.func_800E7660(psx.cptr(c.actorSettingsState, s));
    } else {
        const entry = v.g.D_800F1790()[@as(usize, (actor.flags.raw >> 4) & 31)];
        psx.put(&s.callback, @as(?*anyopaque, @constCast(entry)));
    }
    s.f16 = 16;
    c.func_800E5998();
    func_800E5710(@ptrCast(s));
}
