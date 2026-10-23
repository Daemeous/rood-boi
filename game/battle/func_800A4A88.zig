//! Battle actor contact state: the contact offset and contact ID (src/BATTLE/BATTLE.PRG/3A1A0.c).
const psx = @import("common").psx;
const v = @import("views_3A1A0");
const c = @import("c_3A1A0");

/// orig: func_800A4A88 (src/BATTLE/BATTLE.PRG/3A1A0.c)
/// Updates an actor's contact for `mode`: 0 moves it by the contact offset, 1 prepares a new contact, 2 commits the contact ID.
pub fn func_800A4A88(actor: ?*v.D_800F4538_t, mode: c_int) callconv(.c) void {
    const a = actor.?;
    // Uninitialised in the original for a mode other than 0..2: reads address 0, as the PS1 would.
    var contact: *align(1) v.func_8008D2C0_t = psx.lowRam(v.func_8008D2C0_t);

    switch (mode) {
        0 => {
            if (psx.getBits(&a.unk0, "unkC_0") == 0) return;
            contact = @ptrCast(c.func_800A4A24(@as(c_int, psx.getBits(&a.unk0, "unkC_0"))));
            a.unk6EE = psx.conv(u8, @as(c_int, contact.unk0) -% @as(c_int, psx.byteAt(i16, a, 0x17EC).*));
            a.unk6EF = psx.conv(u8, @as(c_int, contact.unk4) -% @as(c_int, psx.byteAt(i16, a, 0x17F0).*));
            a.unk0.position.vx = psx.conv(i16, @as(c_int, a.unk0.position.vx) +% @as(c_int, psx.conv(i8, a.unk6EE)));
            a.unk0.position.vz = psx.conv(i16, @as(c_int, a.unk0.position.vz) +% @as(c_int, psx.conv(i8, a.unk6EF)));
            a.unk0.position.vy = psx.conv(i16, @as(c_int, a.unk0.position.vy) +%
                (@as(c_int, contact.unk2) -% @as(c_int, psx.byteAt(i16, a, 0x17EE).*)));
        },
        1 => {
            const movement: c_uint = psx.byteAt(c_uint, a, 8).* & 0x70000;
            if (movement == 0x20000 or movement == 0x30000) return;
            const contactId: c_int = @as(c_int, psx.getBits(&a.unk0, "unkC_0"));
            if (@as(c_int, psx.getBits(&a.unk0, "unkC_4")) != contactId) {
                if (a.unk0.unkF == 0 and contactId != 0 and
                    contactId != @as(c_int, psx.byteAt(i16, a, 0x17F2).*))
                {
                    c.func_800E4BE0(contactId);
                }
                a.unk6EE = 0;
                a.unk6EF = 0;
            }
            if (psx.getBits(&a.unk0, "unkC_0") == 0) return;
            contact = @ptrCast(c.func_800A4A24(@as(c_int, psx.getBits(&a.unk0, "unkC_0"))));
        },
        2 => {
            const current: c_int = @as(c_int, psx.getBits(&a.unk0, "unkC_0"));
            const previous: c_int = @as(c_int, psx.getBits(&a.unk0, "unkC_4"));
            if (current != previous) {
                if (psx.getBits(&a.unk0, "unkC_0") != 0) {
                    psx.byteAt(i16, a, 0x17F2).* = psx.conv(i16, current);
                    c.func_8008C49C(@as(c_int, psx.getBits(&a.unk0, "unkC_0")) -% 2, @as(c_int, a.unk0.unkF));
                } else {
                    c.func_8008C49C(previous -% 2, -1);
                    if ((psx.byteAt(c_uint, a, 8).* & 0x70000) == 0) {
                        a.unk6EE = 0;
                        a.unk6EF = 0;
                    }
                }
            }
            if (a.unk0.unkF == 0 and (psx.byteAt(c_uint, a, 8).* & 0x70000) == 0 and
                @as(c_int, psx.getBits(&a.unk0, "unkC_0")) != @as(c_int, psx.byteAt(i16, a, 0x17F2).*))
            {
                c.func_800E4BD8(@as(c_int, psx.byteAt(i16, a, 0x17F2).*));
                psx.byteAt(i16, a, 0x17F2).* = 0;
            }
            a.unk0.unk5D = psx.conv(u8, psx.getBits(&a.unk0, "unkC_0"));
            psx.setBits(&a.unk0, "unkC_4", psx.getBits(&a.unk0, "unkC_0"));
            return;
        },
        else => {},
    }

    psx.byteAt(i16, a, 0x17EC).* = contact.unk0;
    psx.byteAt(i16, a, 0x17EE).* = contact.unk2;
    psx.byteAt(i16, a, 0x17F0).* = contact.unk4;
}
