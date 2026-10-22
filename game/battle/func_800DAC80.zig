//! Starts an action: validates it, sets up the context and its cooldown (src/BATTLE/BATTLE.PRG/71618.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_71618");
const c = @import("c_71618");

/// orig: func_800DAC80 (src/BATTLE/BATTLE.PRG/71618.c)
/// Sets up `action` in `context` and starts its cooldown; returns 1 when the action was started.
pub fn func_800DAC80(context: ?*v.actionSetupContext, action: ?*v.func_800DCAA0_t) callconv(.c) c_int {
    if (c.func_800DBD80(@ptrCast(context), @ptrCast(action)) == 0) return 0;
    c.func_800D836C(@ptrCast(context));
    if (c.func_800D826C(@ptrCast(context)) == 0) return 0;

    const ctx = context.?;
    const act = action.?;
    psx.put(&ctx.action, act);
    v.g.D_800F58E0().* = 1;

    const actor = v.g.D_800F4538()[psx.getBits(act, "unkC_0")];
    if (actor != null and (psx.byteAt(c_uint, actor.?, 8).* & 0x180000) != 0) {
        psx.setBits(act, "unkC_4", psx.conv(u3, c.func_800E45D4(2)));
        psx.setBits(act, "unkC_7", 217);
    }

    c.func_800DEEFC(@ptrCast(ctx), 15);
    if (c.func_800DBC60(@ptrCast(ctx), @ptrCast(act)) != 0) {
        if (ctx.kind == 0) {
            const bc = v.g.D_800F58BC().*.?;
            ctx.cooldown = psx.byteAt(u8, bc, 8).*;
        } else {
            ctx.cooldown = 50;
        }
    }
    return 1;
}
