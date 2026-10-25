//! Updates slot `slot`'s status record from a 4-byte update: its kind and the two
//! bytes it replaces (src/BATTLE/BATTLE.PRG/30DB0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_30DB0");
const c = @import("c_30DB0");

/// orig: func_8009F9F4 (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Stores an update into the status record of slot `arg0`, from its kind and the record it replaces.
pub fn func_8009F9F4(arg0: c_int, arg1: ?*v.D_800F4538_unk64) callconv(.c) void {
    const t: *v.D_800F4538_t = psx.at(v.g.D_800F4538(), arg0).* orelse return;
    const update: *v.D_800F4538_unk64 = arg1.?;
    const a2: c_int = update.unk0;

    if (a2 == 0) {
        t.unk17E4.unk1 = 0;
        t.unk17E4.unk2 = 0;
    } else if (a2 == 1) {
        if (update.unk3 == 0) {
            t.unk17E4 = update.*;
        } else {
            var sp10: c_int = undefined;
            var sp14: c_int = undefined;

            t.unk17E8 = update.*;
            t.unk17E4.unk3 = update.unk3;
            const v1: c_int = t.unk17E4.unk0;

            if (v1 == 0) {
                c.func_800AD62C(arg0, &sp10, &sp14, arg0);
                t.unk17E4.unk1 = psx.conv(u8, sp10);
                t.unk17E4.unk2 = psx.conv(u8, sp14);
            } else if (v1 == a2) {
                c.func_800AD62C(arg0, &sp10, &sp14, arg0);
                if (t.unk17E4.unk1 == 0) {
                    t.unk17E4.unk1 = psx.conv(u8, sp10);
                }
                if (t.unk17E4.unk2 == 0) {
                    t.unk17E4.unk2 = psx.conv(u8, sp14);
                }
            } else if (v1 == 2) {
                t.unk17E4.unk1 = 0x80;
                t.unk17E4.unk2 = psx.conv(u8, a2);
            }
            if (update.unk1 == 0) {
                t.unk17E4.unk1 = 0;
            }
            if (update.unk2 == 0) {
                t.unk17E4.unk2 = 0;
            }
        }
    }
    t.unk17E4.unk0 = update.unk0;
}
