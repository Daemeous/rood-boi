//! Steers a battle actor by the scratchpad mode: sets its facing vector and starts its move (pc/src/game/BATTLE/BATTLE.PRG/771A8.c).
const common = @import("common");
const pad = @import("common").pad;
const psx = common.psx;
const v = @import("views_771A8");
const c = @import("c_771A8");

/// orig: func_800E1238 (pc/src/game/BATTLE/BATTLE.PRG/771A8.c)
/// Sets an actor's facing vector from its angle, then starts the move that the scratchpad mode selects.
pub fn func_800E1238(arg0: ?*v.vs_battle_aiState, arg1: c_int) callconv(.c) void {
    var sp10: c_int = undefined;
    var sp14: c_int = undefined;
    var value = arg1;

    // The C passes the pointer on before it dereferences it: no unwrap yet.
    if (c.func_800E0918(@ptrCast(arg0), value, 0) != 0) {
        return;
    }

    if (c.func_800E4764(@ptrCast(arg0), &sp10, &sp14) == 0) {
        c.vs_battle_aiRecoverMovement(@ptrCast(arg0));
        return;
    }

    const self = arg0.?;
    self.flags4.b[3] = 1;
    c.vs_battle_aiResolveDirectionalMovement(@ptrCast(self), value, 3);

    if (@as(*const c_int, @ptrFromInt(pad.addr(0x3C8))).* != 5 and @as(*const c_int, @ptrFromInt(pad.addr(0x3C8))).* != 7) {
        c.vs_battle_aiReleaseActorAiReservations(@ptrCast(self));
        c.vs_battle_aiRecoverMovement(@ptrCast(self));
        return;
    }

    if (@as(*const c_int, @ptrFromInt(pad.addr(0x3C8))).* == 5) {
        value = @as(c_int, self.heading) *% 512;
    }

    const angle: c_int = (-%value +% 3072) & 0xFFF;
    const cosine = c.rcos(angle);
    const sine = c.rsin(angle);

    const limit = c.vs_battle_aiLowestReachableGroundHeight(@ptrCast(self), psx.conv(c_uint, value));
    if (limit +% @as(c_int, self.heightBE) < @as(c_int, self.position.v.vy)) {
        self.moveY = -4096;
        self.moveZ = 0;
        self.moveX = 0;
    } else {
        self.moveY = 0;
        self.moveX = cosine;
        self.moveZ = sine;
    }

    // unk8_21 is a 1-bit field: the C shifts it into bit 21 and tests that.
    if ((@as(c_int, psx.getBits(psx.get(self.object.func_800E1238_t).?, "unk8_21")) << 21) == 0) {
        c.func_800E4B18(@ptrCast(self));
    } else {
        self.counter = 0;
        c.vs_battle_aiIssueActorCommand(@ptrCast(self), sp14);
    }
}
