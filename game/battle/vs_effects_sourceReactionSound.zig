//! An effect-script opcode: vs_audio_playActorModelSoundVariant on the target with a given or random variant (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_effects_sourceReactionSound (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// For a kind-4 unkC target: vs_audio_playActorModelSoundVariant(its id byte, variant) for a variant below 4, else a random 0..2 other than the last (vs_effects_lastReactionSoundVariant, which keeps it).
pub fn vs_effects_sourceReactionSound(script: ?*v.vs_effects_EffectInstance, variant: u8) callconv(.c) void {
    const s = script.?;
    if (s.context.unkC.unk0 == 4) {
        if (variant < 4) {
            c.vs_audio_playActorModelSoundVariant(@as(c_int, psx.conv(u8, s.context.unkC.unk4.unk0)), variant);
        } else {
            var pick: c_int = undefined;
            while (true) {
                pick = @rem(c.rand(), 3);
                if (pick != v.g.vs_effects_lastReactionSoundVariant().*) break;
            }
            v.g.vs_effects_lastReactionSoundVariant().* = psx.conv(u8, pick);
            c.vs_audio_playActorModelSoundVariant(@as(c_int, psx.conv(u8, s.context.unkC.unk4.unk0)), v.g.vs_effects_lastReactionSoundVariant().*);
        }
    }
}
