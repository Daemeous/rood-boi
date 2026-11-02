//! An actor's animation reset: catches up the root motion of its current
//! animation's opening frame (src/BATTLE/BATTLE.PRG/44F14.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_44F14");
const c = @import("c_44F14");

/// func_800AD494: the animation's sequence word and its header (no prototype
/// in the unit that takes the actor view).
const LoadAnimFn = *const fn (*v.D_800F4538_t, c_int, *?[*]u16) callconv(.c) c_int;
/// (A function, not a module-level pointer constant: that would be a relocated data slot in the engine library.)
inline fn loadAnimation() LoadAnimFn {
    return @extern(LoadAnimFn, .{ .name = "func_800AD494" });
}

/// The C calls func_800AD714 through `(void (*)())` with a fourth argument
/// the decoder ignores (it reads state->unk544 itself).
const DecodeFn = *const fn (*v.D_800F4538_t, *v.D_800F4538_unkC54, c_int, *u8) callconv(.c) void;
/// (A function, not a module-level pointer constant: that would be a relocated data slot in the engine library.)
inline fn decode() DecodeFn {
    return @extern(DecodeFn, .{ .name = "func_800AD714" });
}

/// orig: func_800AE47C (src/BATTLE/BATTLE.PRG/44F14.c)
/// Points the actor's decoder (unk704) at the first op of its animation's
/// sequence (an 0xFF escape takes a second count byte) and runs it once in
/// mode 2. With no sequence loaded (PC guard) it logs that once and returns.
pub fn func_800AE47C(arg0: ?*v.D_800F4538_t) callconv(.c) void {
    const actor = arg0.?;
    var header: ?[*]u16 = undefined;
    const word: u32 = @bitCast(loadAnimation()(actor, @as(c_int, actor.animationId), &header));
    if (word == 0 or header == null) {
        // The C's block-scope `static int reported` (globalised by the build).
        const reported = v.g.func_800AE47C__reported();
        const before = reported.*;
        reported.* = before +% 1;
        if (before == 0) {
            var line: [96]u8 = undefined;
            _ = c.sprintf(&line, "[actor] slot %d: animation %d has no sequence loaded",
                @as(c_int, actor.unk0.unkF), @as(c_int, actor.animationId));
            c.pc_log(&line);
        }
        return;
    }
    const sequence: [*]u8 = @ptrFromInt(@as(usize, psx.byteAt(u32, @as(*u8, @ptrFromInt(@as(usize, word))), 0).*));
    const op = sequence + header.?[2];

    var count: c_int = op[0];
    var next = op + 1;
    if (count == 0xFF) {
        next = op + 2;
        count = @as(c_int, op[1]) + 0xFF;
    }

    actor.unk704.unk542 = psx.conv(u16, count);
    psx.put(&actor.unk704.unk544, next);

    decode()(actor, &actor.unk704, 2, &next[0]);
}
