//! Queues a stat-change display entry for a weapon or armor stat change (src/BATTLE/BATTLE.PRG/2842C.c).
const psx = @import("common").psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: func_80096768 (src/BATTLE/BATTLE.PRG/2842C.c)
/// Queues a stat-change display entry when the matching setting is on, after counting the queued kind-6 and kind-7 entries.
pub fn func_80096768(stat: c_int, slot: c_int, value: c_int) callconv(.c) c_int {
    const settings = v.g.vs_main_settings();
    if ((psx.getBits(settings, "weaponStatChange") != 0 and stat == 240) or
        (psx.getBits(settings, "armorStatChange") != 0 and stat != 240))
    {
        var kind: c_int = stat;
        switch (kind) {
            1 => {
                kind = 4;
            },
            2 => {
                kind = 2;
            },
            3 => {
                kind = 3;
            },
            4 => {
                kind = 6;
            },
            6 => {
                kind = 5;
            },
            241 => {
                kind = 1;
            },
            240 => {
                kind = 0;
            },
            else => {},
        }

        const base: [*c]v.D_800F1BAC_t = @ptrCast(@alignCast(v.g.D_800F1BAC().*));
        var count: c_int = 0;
        var i: c_int = 0;
        while (i < @as(c_int, v.g.D_800F227E().*)) : (i +%= 1) {
            const entry = psx.at(base, i);
            if (entry.unk8.u8[0] == 6 or entry.unk8.u8[0] == 7) {
                count +%= 1;
            }
        }

        const p: *v.D_800F1BAC_t = @ptrCast(@alignCast(c.func_800962E4()));
        p.unk8.u8[0] = 7;
        p.unk8.u8[1] = 0x7F;
        p.unk6 = psx.conv(i16, count);
        p.unk12 = 0;
        p.unkB = 0;
        p.unkE = psx.conv(u8, kind);
        p.unkA = psx.conv(u8, slot);

        const sign: c_uint = (psx.conv(c_uint, value) & 0x8000_0000) >> 16;
        if ((value & 0x7FFF) >= 1000) {
            p.unkC = psx.conv(u16, @as(c_uint, 999) | sign);
        } else {
            p.unkC = psx.conv(u16, (psx.conv(c_uint, value) & 0x7FFF) | sign);
        }

        _ = c.sprintf(&p.displayText, v.g.D_800691E4(), @as(c_int, p.unkC & 0x7FFF));
        p.displayTextLen = psx.conv(u8, c.strlen(&p.displayText));
        v.g.D_800F227E().* +%= 1;
    }

    return 0;
}
