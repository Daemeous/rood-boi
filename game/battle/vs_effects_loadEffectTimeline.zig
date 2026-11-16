//! Rebuilds the per-scene block-3 table held in arg0 (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_effects_loadEffectTimeline (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Replaces the entry table at arg0's block-3 slot with the entries of scene arg1.
pub fn vs_effects_loadEffectTimeline(arg0: ?*v.D_800F53B8_t, arg1: c_int, _: c_int) callconv(.c) void {
    const owner = arg0.?;
    const entry_raw = c._getPFileBlock3section(psx.cptr(c.pFileBlock3, psx.get(v.g.vs_effects_package().*.?.block3Data).?), arg1);
    const entry: *align(1) v.func_800D55A4_t = @ptrCast(entry_raw.?);
    var count: c_int = 0;
    var i: c_int = 0;

    if (psx.get(owner.context.unk30) != null) {
        c.vs_main_freeHeapR(psx.get(owner.context.unk30));
    }

    const size: c_int = entry.count *% @as(c_int, @sizeOf(v.func_800D5780_t)) +% @as(c_int, @offsetOf(v.D_800F53B8_t4, "tracks"));
    psx.put(&owner.context.unk30, c.vs_main_allocHeapR(psx.conv(c_uint, size)));
    const p = psx.get(owner.context.unk30).?;
    p.trackCount = psx.conv(u8, entry.count);
    p.frame = 0;
    p.activeTracks = 0;

    // i < 256: the entry count is a u8, and p->unk0 is reread each pass as the C does.
    while (i < @as(c_int, p.trackCount)) : (i += 1) {
        const slot = psx.at(&p.tracks, i);
        psx.put(&slot.keys, c.vs_content_pFileTimelineTrackData(psx.cptr(c.pFileBlock3, psx.get(v.g.vs_effects_package().*.?.block3Data).?), arg1, i));
        slot.header = psx.at(&entry.tracks, i).header;

        if (psx.getBits(&slot.header, "unk0_0") == 3) {
            count +%= 1;
        }

        slot.keyIndex = -1;
        slot.unk8 = 0;
        slot.unkA = 0;
        slot.unkC = 0;
        _ = c.vs_effects_advanceTimelineKey(@ptrCast(slot));
        // i & 31: the C shifts by i, and x86 takes the count modulo 32.
        p.activeTracks |= @as(i32, @bitCast(@as(c_uint, 1) << psx.conv(u5, i)));
    }

    p.unk1 = if (count != 0) 2 else 0;
}
