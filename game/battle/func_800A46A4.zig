//! Sweeps the actor's collision probe over the scratchpad contact slots (src/BATTLE/BATTLE.PRG/3A1A0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_3A1A0");
const c = @import("c_3A1A0");

/// orig: func_800A46A4 (src/BATTLE/BATTLE.PRG/3A1A0.c)
/// Clears the actor's contact slots at its probe position, then reports whether any of the eight slots is set.
pub fn func_800A46A4(actor: ?*v.D_800F4538_t) callconv(.c) c_int {
    const a = actor.?;
    const scratch: *v.collisionScratchView = @ptrFromInt(0x1F8003BC);

    const saved = psx.getBits(&a.unk0, "unkC_0");
    const radius: c_int = a.unk63C;

    psx.setBits(&a.unk0, "unkC_0", 0);
    psx.setBits(&a.unk0, "unkA_3", 0);
    psx.setBits(&a.unk0, "unk9_6", 0);
    _ = c.func_800A70DC(@ptrCast(a), @divTrunc(c.rsin(512) *% radius, 4096));
    psx.setBits(&a.unk0, "unkC_0", saved);
    psx.setBits(&a.unk0, "unkA_3", 2);

    var direction: c_int = @as(c_int, psx.conv(u16, a.unk1800)) >> 9;
    const hit = psx.byteAt(v.collisionScratchView, scratch, @as(isize, direction));
    hit.contact = 0;
    hit.nextContact = 0;

    direction = (direction -% 1) & 7;
    const previous = psx.byteAt(v.collisionScratchView, scratch, @as(isize, direction));
    previous.contact = 0;

    var i: c_int = 0;
    while (i < 8) : (i +%= 1) {
        if (psx.byteAt(v.collisionScratchView, scratch, @as(isize, i)).contact != 0) {
            return 1;
        }
    }
    return 0;
}
