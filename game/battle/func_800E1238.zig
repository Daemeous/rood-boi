//! Steers a battle actor by the scratchpad mode: sets its facing vector and starts its move (src/BATTLE/BATTLE.PRG/771A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_771A8");
const c = @import("c_771A8");

/// orig: func_800E1238 (src/BATTLE/BATTLE.PRG/771A8.c)
/// Sets an actor's facing vector from its angle, then starts the move that the scratchpad mode selects.
pub fn func_800E1238(arg0: ?*v.func_800E0850_t, arg1: c_int) callconv(.c) void {
    var sp10: c_int = undefined;
    var sp14: c_int = undefined;
    var value = arg1;

    // The C passes the pointer on before it dereferences it: no unwrap yet.
    if (c.func_800E0918(@ptrCast(arg0), value, 0) != 0) {
        return;
    }

    if (c.func_800E4764(@ptrCast(arg0), &sp10, &sp14) == 0) {
        c.func_800E2CCC(@ptrCast(arg0));
        return;
    }

    const self = arg0.?;
    self.unk7 = 1;
    c.func_800E3600(@ptrCast(self), value, 3);

    if (@as(*const c_int, @ptrFromInt(0x1F8003C8)).* != 5 and @as(*const c_int, @ptrFromInt(0x1F8003C8)).* != 7) {
        c.func_800E678C(@ptrCast(self));
        c.func_800E2CCC(@ptrCast(self));
        return;
    }

    if (@as(*const c_int, @ptrFromInt(0x1F8003C8)).* == 5) {
        value = @as(c_int, self.unk188) *% 512;
    }

    const angle: c_int = (-%value +% 3072) & 0xFFF;
    const cosine = c.rcos(angle);
    const sine = c.rsin(angle);

    const limit = c.func_800E0850(@ptrCast(self), psx.conv(c_uint, value));
    if (limit +% @as(c_int, self.unkBE) < @as(c_int, self.unk4E)) {
        self.unk184 = -4096;
        self.unk180 = 0;
        self.unk17C = 0;
    } else {
        self.unk184 = 0;
        self.unk17C = cosine;
        self.unk180 = sine;
    }

    // unk8_21 is a 1-bit field: the C shifts it into bit 21 and tests that.
    if ((@as(c_int, psx.getBits(psx.get(self.unk58).?, "unk8_21")) << 21) == 0) {
        c.func_800E4B18(@ptrCast(self));
    } else {
        self.unk228 = 0;
        c.func_800DEEFC(@ptrCast(self), sp14);
    }
}
