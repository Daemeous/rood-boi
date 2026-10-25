//! Drops or animates a battle actor from its slot, or resets its object slot when the actor is gone (src/BATTLE/BATTLE.PRG/30DB0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_30DB0");
const c = @import("c_30DB0");

/// orig: func_8009F314 (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Stops an actor's motion and starts its fall, then plays its animation (or holds it), and does the same for the actor linked to it; an empty slot resets its object instead.
pub fn func_8009F314(actorId: c_int, context: ?*anyopaque, animate: c_int) callconv(.c) void {
    if (psx.at(v.g.D_800F4538(), actorId).*) |actor| {
        const flags0: c_int = psx.byteAt(c_int, actor, 8).*;
        if ((flags0 & 0x270000) != 0 or (flags0 & 0x180000) == 0x100000) {
            const low = psx.getBits(&actor.unk0, "unkC_0");
            if (low != 0) {
                c.func_8008C49C(@as(c_int, low) -% 2, -1);
                psx.setBits(&actor.unk0, "unkC_0", 0);
            }
            actor.unk0.unk1A = 0;
            actor.unk0.unk34.vx = 0;
            actor.unk0.unk34.vz = 0;
            // The vertical speed is kept only when falling-free: unkF == 0, mode 3, vy > 0.
            const keepVy = actor.unk0.unkF == 0 and
                (psx.byteAt(c_int, actor, 8).* & 0x70000) == 0x30000 and
                actor.unk0.unk34.vy > 0;
            if (!keepVy) actor.unk0.unk34.vy = -6;
            actor.unk181A = 0;
            psx.byteAt(c_int, actor, 8).* = (psx.byteAt(c_int, actor, 8).* & ~@as(c_int, 0x70000)) | 0x30000;
        }
        if (animate != 0) {
            var animationId: c_int = 0x46;
            if (psx.getBits(actor, "unk5AC_3") != 0) animationId = 0x3A;
            // Uninitialised in the original; the callee's result decides it, so it starts null here.
            var animation: [*c]c.u_short = null;
            _ = c.func_800AD494(@ptrCast(actor), 0xAB, &animation);
            if (animation != null) {
                if ((psx.byteAt(c_int, actor, 8).* & 0x70000) == 0) animationId = 0x47;
            }
            _ = c.func_800A0204(actorId, animationId, 0, 6);
            c.func_8009FD38(@ptrCast(actor));
            psx.setBits(&actor.unk0, "unk9_0", 1);
        } else {
            psx.setBits(&actor.unk0, "unk9_0", 2);
        }
        if (actor.unk17FD >= 2) {
            c.func_8009F314(@as(c_int, actor.unk17FD), context, animate);
        }
    } else {
        if (psx.at(v.g.D_800F45E0(), actorId).*) |object| {
            object.unk1A = 0xF7;
            psx.byteAt(u8, object, 0x170).* = 0;
            _ = c.func_800AE7D8(@ptrCast(object), 0x32, 0);
        }
    }
}
