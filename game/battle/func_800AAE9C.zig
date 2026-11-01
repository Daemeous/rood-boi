//! Steps an actor's two texture animation slots through their frame tables
//! and redraws the frame each one shows (src/BATTLE/BATTLE.PRG/40564.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_40564");
const c = @import("c_40564");

/// orig: func_800AAE9C (src/BATTLE/BATTLE.PRG/40564.c)
/// Advances the texture animation slots of `actor` (frame timers, table wrap) and draws each slot's frame.
pub fn func_800AAE9C(actor: ?*v.D_800F4538_t) callconv(.c) c_int {
    if (v.g.D_800F2450()[v.g.D_800E8F2C().*] == 0) {
        return 0;
    }
    // The C's bare `return;` and its end return nothing; the 64-bit object
    // returns the slot index (1 at the end), the 32-bit one the actor: every
    // caller declares the function void.
    var i: c_int = 0;
    while (i < 2) : (i += 1) { // i < 2: no overflow
        const slot = &actor.?.unk180C[@intCast(i)];
        if (psx.getBits(slot, "unk0_0") == 0) continue;
        const mode = psx.getBits(slot, "unk0_4");
        if (mode == 2) continue;
        if (mode == 0 and slot.unk1 == 1) {
            psx.setBits(slot, "unk0_0", 0);
            continue;
        }
        if (mode == 0 or mode == 1) {
            const row: c_uint = psx.getBits(slot, "unk0_0");
            var p: *v.func_800AAE9C_t = if (i == 0)
                psx.at(@as([*]v.func_800AAE9C_t, @ptrCast(psx.at(v.g.D_800E909C(), row))), slot.unk2)
            else
                psx.at(@as([*]v.func_800AAE9C_t, @ptrCast(psx.at(v.g.D_800E916C(), row))), slot.unk2);

            slot.unk3 +%= 1;

            if (p.unk1 < slot.unk3) {
                slot.unk3 = 0;
                slot.unk2 +%= 1;
                p = psx.at(p, 1);

                if (p.unk0 == 0) {
                    slot.unk2 = 0;
                    p = if (i == 0)
                        &psx.at(v.g.D_800E909C(), psx.getBits(slot, "unk0_0"))[0]
                    else
                        &psx.at(v.g.D_800E916C(), psx.getBits(slot, "unk0_0"))[0];
                }
            }

            if (p.unk0 == 0) {
                return i;
            }

            slot.unk1 = p.unk0;
        }
        c.func_800AB098(@ptrCast(actor.?), i, @as(c_int, slot.unk1) -% 1);
    }
    return 1;
}
