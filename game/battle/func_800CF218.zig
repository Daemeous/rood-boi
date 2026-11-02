//! Spawns the child scripts of an effect request (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// psx_set of a guest address: the 4-byte field's address on 64-bit, the pointer's bits on 32-bit (stored as
/// the C stores it: by its bits, so an unaligned address does not trap).
fn setAddress(field: anytype, address: u32) void {
    const F = @typeInfo(@TypeOf(field)).pointer.child;
    switch (@typeInfo(F)) {
        .optional, .pointer => @as(*usize, @ptrCast(field)).* = address,
        else => field.raw = address,
    }
}

/// orig: func_800CF218 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Saves the request in D_800F53C0 and spawns one child script per target (unk2 of them) with the request's token; returns the token.
pub fn func_800CF218(request_arg: ?*v.func_800CF0E8_t, arg1: c_int, arg2: c_int) callconv(.c) u8 {
    _ = arg1;
    const request = request_arg.?;
    var spawn: v.D_800F53B8_t2 = undefined; // unk21..unk24 stay uninitialised, as in the C
    const token = psx.conv(u8, c.func_800CF49C());
    const base: u32 = psx.addr(&request.unk4); // (D_800F53B8_t3*)&arg0->unk4
    setAddress(&spawn.unk0, base);
    setAddress(&spawn.unk14, psx.rawOf(v.g.D_800F569C().*.?.block8Data));
    spawn.unk18 = @as(c_int, request.effectId) * 3;
    psx.put(&spawn.unk1C, null);
    spawn.unk20 = token;
    v.g.D_800F53C0().* = request.*;
    var i: c_int = 0;
    while (i < request.unk2) : (i += 1) { // i < a u8 count
        const target = psx.byteAt(v.D_800F53B8_t3, request, i * 12 + 16); // (char*)arg0 + i * 12 + 16
        const id: c_int = request.effectId;
        if ((id >= 40 and id <= 41) or (id >= 44 and id <= 53)) {
            setAddress(&spawn.unk4, psx.addr(target));
            setAddress(&spawn.unkC, base);
        } else {
            switch (psx.conv(u16, id)) {
                23, 25 => {
                    setAddress(&spawn.unk4, psx.addr(target));
                    setAddress(&spawn.unkC, base);
                },
                else => {
                    setAddress(&spawn.unk4, base);
                    setAddress(&spawn.unkC, psx.addr(target));
                },
            }
        }
        setAddress(&spawn.unk8, psx.addr(target));
        spawn.unk10 = psx.byteAt(u16, request, i * 12 + 16).*;
        const child = c.func_800CF55C(@ptrCast(&spawn)).?;
        const s: *v.D_800F53B8_t = @ptrCast(@alignCast(child));
        s.unkD1C.unk3C = arg2;
        s.unkD1C.unk38 = @bitCast(request.effectId);
        if (request.effectId == 54) {
            psx.setBits(s, "unk14_8", 4);
        } else {
            psx.setBits(s, "unk14_8", 2);
        }
        c.func_800CF484(3, child);
    }
    return token;
}
