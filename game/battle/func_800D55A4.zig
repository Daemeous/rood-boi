//! Rebuilds the per-scene block-3 table held in arg0 (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800D55A4 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Replaces the entry table at arg0's block-3 slot with the entries of scene arg1.
pub fn func_800D55A4(arg0: ?*v.D_800F53B8_t, arg1: c_int, _: c_int) callconv(.c) void {
    const owner = arg0.?;
    const entry_raw = c._getPFileBlock3section(psx.cptr(c.pFileBlock3, psx.get(v.g.D_800F569C().*.?.block3Data).?), arg1);
    const entry: *align(1) v.func_800D55A4_t = @ptrCast(entry_raw.?);
    var count: c_int = 0;
    var i: c_int = 0;

    if (psx.get(owner.unkD1C.unk30) != null) {
        c.vs_main_freeHeapR(psx.get(owner.unkD1C.unk30));
    }

    const size: c_int = entry.count *% 16 +% 8;
    psx.put(&owner.unkD1C.unk30, c.vs_main_allocHeapR(psx.conv(c_uint, size)));
    const p = psx.get(owner.unkD1C.unk30).?;
    p.unk0 = psx.conv(u8, entry.count);
    p.unk2 = 0;
    p.unk4 = 0;

    // i < 256: the entry count is a u8, and p->unk0 is reread each pass as the C does.
    while (i < @as(c_int, p.unk0)) : (i += 1) {
        const slot = psx.at(&p.unk8, i);
        psx.put(&slot.unk0, c.func_800D5564(psx.cptr(c.pFileBlock3, psx.get(v.g.D_800F569C().*.?.block3Data).?), arg1, i));
        slot.unk4 = psx.at(&entry.unk4, i).unk0;

        if (psx.getBits(&slot.unk4, "unk0_0") == 3) {
            count +%= 1;
        }

        slot.unk6 = -1;
        psx.byteAt(c_int, slot, 8).* = 0;
        slot.unkC = 0;
        _ = c.func_800D5780(@ptrCast(slot));
        // i & 31: the C shifts by i, and x86 takes the count modulo 32.
        p.unk4 |= @as(i32, @bitCast(@as(c_uint, 1) << psx.conv(u5, i)));
    }

    p.unk1 = if (count != 0) 2 else 0;
}
