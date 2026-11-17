//! Resets a scene record and copies its links and fields from the source block
//! (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const psx = @import("common").psx;
const v = @import("views_5BF94");

/// orig: vs_effects_initEffectInstance (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Clears a scene record's sixteen entries and copies the source block's links and fields into it.
pub fn vs_effects_initEffectInstance(src_block: ?*v.vs_effects_EffectSpawnRequest, dst_block: ?*v.vs_effects_EffectInstance) callconv(.c) void {
    const src = src_block.?;
    const dst = dst_block.?;

    psx.put(&dst.previous, psx.get(src.parent));
    dst.unk8 = 0;
    dst.unk9 = psx.conv(u8, src.priority);
    dst.cursor = psx.conv(u16, src.scriptOffset);
    dst.unkC = src.script;
    @as(*align(1) i32, @ptrCast(&dst.unk10)).* = 0;
    psx.setBits(dst, "unk14_0", src.token);
    psx.put(&dst.unk18, null);
    psx.setBits(dst, "unk14_11", 0);
    psx.setBits(dst, "unk14_16", 0);

    for (&dst.unk1C) |*entry| {
        psx.fieldAt(i16, &entry.unk4, 2).* = 0;
        entry.unk4 = 0;
        entry.unk0 = 0;
        entry.unkC = 0;
        entry.unk8 = 0;
        entry.unk18 = 0;
        entry.unk14 = 0;
        entry.unk10 = 0;
        entry.unk30 = 0;
        entry.unk2C = 0;
        entry.unk28 = 0;
        @as(*align(1) i32, @ptrCast(&entry.unk34)).* = 0;
    }

    dst.context.unk0 = psx.get(src.source).?.unk0;
    dst.context.unkC = psx.get(src.emitFrom).?.unk0;
    dst.context.unk18 = psx.get(src.target).?.unk0;
    dst.context.unk24 = psx.get(src.emitTo).?.unk0;

    psx.put(&dst.context.unk30, null);
    psx.put(&dst.context.unk34, null);
    dst.context.unk3C = 0;
}
