//! Steps the command scripts of a list of entries through their handlers
//! (src/BATTLE/BATTLE.PRG/2842C.c).
const psx = @import("common").psx;
const v = @import("views_2842C");

/// orig: func_80092B04 (src/BATTLE/BATTLE.PRG/2842C.c)
/// Runs each of the arg1 entries' command scripts until it ends; returns nonzero unless every entry is idle.
pub fn func_80092B04(arg0: ?*v.func_80092B04_t, arg1: c_int, _: ?*v.D_800F1DD8_t) callconv(.c) c_int {
    const Handler = *const fn (?*anyopaque, ?*anyopaque) callconv(.c) c_int;

    var idle: c_int = 0;
    const entries: [*c]v.func_80092B04_t = arg0;
    var i: c_int = 0;
    while (i < arg1) : (i +%= 1) {
        const entry = &entries[@intCast(i)];
        if (psx.get(entry.unk4) != null) {
            while (true) {
                const cmd: *align(1) v.func_80092B04_t2 = @ptrCast(psx.get(entry.unk4).?);
                if (cmd.unk0 != 0 and cmd.unk2 < 0xD) {
                    const flagged = v.g.vs_main_stateFlags().unkA8 != 0;
                    if (flagged) {
                        if (cmd.unk2 == 0xB) break;
                    }
                    // The handler is looked up again on every call: it may change the script.
                    var stop = false;
                    while (true) {
                        const slot: ?*const anyopaque = psx.at(v.g.D_800E85E8(), cmd.unk2).*;
                        const handler: Handler = @ptrCast(slot.?);
                        if (handler(entry, cmd) == 0) break;
                        if (!flagged) {
                            stop = true;
                            break;
                        }
                    }
                    if (stop) break;
                    psx.put(&entry.unk4, psx.byteAt(u8, psx.get(entry.unk4).?, @as(isize, cmd.unk0)));
                } else {
                    psx.put(&entry.unk4, null);
                    break;
                }
            }
        } else {
            idle +%= 1;
        }
    }
    return @as(c_int, @intFromBool(idle != arg1));
}
