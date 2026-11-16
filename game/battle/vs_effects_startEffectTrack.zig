//! Runs a pending block-8 text for slot `arg0` through vs_effects_createPrimaryEffectInstance, then clears its pending bit
//! (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_effects_startEffectTrack (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Hands slot `arg0`'s pending text to vs_effects_createPrimaryEffectInstance once and clears its pending bit; returns the status of func_800CEEBC.
pub fn vs_effects_startEffectTrack(arg0: c_int) callconv(.c) c_int {
    // unk24 is never written here: it is uninitialised in the original too.
    var sp10: v.D_800F53B8_t2 = undefined;

    c.vs_effects_selectEffectResourceContext(1);
    const result = c.func_800CEEBC();
    if (result != 0) {
        // The shift count is masked to 5 bits, as the x86 object does for an out-of-range arg0.
        const bit: c_uint = @as(c_uint, 1) << @as(u5, @truncate(psx.conv(c_uint, arg0)));
        const flags = v.g.vs_effects_pendingTracks();
        if ((flags.* & bit) != 0) {
            const a1 = psx.get(v.g.vs_effects_package().*.?.block8Data);
            // The PS1 read the text-run records straight out of the effect parameter block (vs_battle_legacyUnusedTargetBlock =
            // vs_effects_actionRequest + 4, 12-byte records at +0 and +0xC) through t3 pointers; vs_effects_initEffectInstance only copies
            // each one's first record, so two local t3s carry them.
            const run = &v.g.vs_effects_actionRequest().unk4;
            var first: v.D_800F53B8_t3 = undefined;
            var second: v.D_800F53B8_t3 = undefined;
            first.unk0 = psx.fieldAt(v.D_800F53B8_t3_2, run, 0).*;
            second.unk0 = psx.fieldAt(v.D_800F53B8_t3_2, run, 0xC).*;
            sp10.scriptOffset = arg0 *% 3;
            psx.put(&sp10.source, &first);
            psx.put(&sp10.emitFrom, &first);
            sp10.priority = 0;
            psx.put(&sp10.target, &second);
            psx.put(&sp10.emitTo, &second);
            psx.put(&sp10.parent, null);
            sp10.token = v.g.vs_effects_primaryToken().*;
            psx.put(&sp10.script, a1);
            _ = c.vs_effects_createPrimaryEffectInstance(psx.cptr(c.D_800F53B8_t2, &sp10));
            c.vs_effects_setPrimaryEffectPhase(4);
            v.g.vs_effects_startedTrackCount().* +%= 1;
            flags.* &= ~bit;
        }
    }
    return result;
}
