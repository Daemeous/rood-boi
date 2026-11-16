//! Runs the pending entries (indices 1..15) of the table D_800F2260 through func_80095C18 while vs_battle_actorMarkersHidden is clear (pc/src/game/BATTLE/BATTLE.PRG/2842C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

const SiteFn = *const fn (c_int, ?*anyopaque, c_int, c_int, ?*anyopaque, ?*anyopaque) callconv(.c) void;

/// classic_hook.h PC_CLASSIC_SITE_EMOTE (a macro: not in `c`).
const site_emote: c_int = 15;

/// The packet cursor.
fn scratchPtr() ?*anyopaque {
    return common.scratch.get().unk0;
}

/// orig: func_8009627C (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Calls func_80095C18 for each nonzero entry 1..15 of D_800F2260, unless vs_battle_actorMarkersHidden is set.
pub fn func_8009627C() callconv(.c) void {
    if (v.g.vs_battle_actorMarkersHidden().* == 0) {
        var i: c_int = 1;
        while (i < 16) : (i +%= 1) {
            const table = v.g.D_800F2260();
            if (table[@intCast(i)] != 0) {
                // PC_CLASSIC (classic_hook.h): the emote func_80095C18 decides, recorded as it decides it.
                const record = v.g.pc_emote_record();
                record.draw = 0;
                const first = scratchPtr();
                c.func_80095C18(i, table[@intCast(i)]);
                if (psx.weak(SiteFn, "pc_classic_site")) |site| site(site_emote, record, i, 0, first, scratchPtr());
            }
        }
    }
}
