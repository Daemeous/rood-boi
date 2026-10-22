//! Plays a sound for an actor record, panned and voiced from the actor's sound
//! values (src/BATTLE/BATTLE.PRG/44F14.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_44F14");
const c = @import("c_44F14");

/// orig: func_800AE4FC (src/BATTLE/BATTLE.PRG/44F14.c)
/// Plays sound `kind` at the record's position: panned, with the actor's sound overrides applied.
pub fn func_800AE4FC(arg0: ?*v.D_800F4538_unk0, arg1: c_int) callconv(.c) void {
    const rec = arg0.?;
    const kind: c_int = arg1;

    if (@as(c_int, rec.position.vy) >= 0xBB8) return;

    const actor = psx.at(v.g.D_800F4538(), rec.unkF).*.?;

    var sp0: c_int = undefined;
    var sp1: c_int = undefined;
    if (actor.unk17E4.unk0 == 2) {
        sp0 = 0x80;
        sp1 = 0;
    } else {
        // The int at byte 8 of unk0: `(int*)&nQuads + 1`.
        if ((psx.byteAt(c_int, &actor.unk0, 8).* & 0x3F9F00) != 0x8000) {
            _ = c.func_800A1280(@as(c_int, rec.unkF), 0xFF, @ptrCast(&actor.unk6FC), 0);
        }

        const temp = c.vs_main_computeSfxPan(psx.byteAt(c_int, &actor.unk6FC, 0).*, @as(c_int, actor.unk6FC.vz));
        sp0 = temp >> 16;
        if (actor.unk17E4.unk1 != 0) {
            sp0 = @as(c_int, actor.unk17E4.unk1);
        }

        sp1 = temp & 0xFFFF;
        if (actor.unk17E4.unk2 != 0) {
            sp1 = @as(c_int, actor.unk17E4.unk2);
        }
    }

    if (sp1 == 0) return;

    const akao_raw: *align(4) anyopaque = @alignCast(psx.get(psx.get(rec.unk68).?.akaoOffset).?);
    const akao: [*c]c_uint = @ptrCast(akao_raw);

    if (kind == 8) {
        _ = c.func_8006CD60(@ptrCast(akao), sp0, sp1);
    } else if (kind == 4) {
        c.func_800461CC(0x7E, akao, psx.conv(c_uint, @rem(c.rand(), 4) +% 4), sp0, sp1);
    } else {
        c.func_800461CC(0x180, akao, psx.conv(c_uint, kind), sp0, sp1);
    }
}
