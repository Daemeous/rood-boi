//! An actor's animation tables (src/BATTLE/BATTLE.PRG/40564.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_40564");

const Table = v.func_800AD494_t;

/// A table pointer from a `void*` field: word-aligned (the PS1 code loads its words with lw).
fn table(p: ?*anyopaque) ?*Table {
    return @ptrCast(@alignCast(p));
}

/// orig: func_800AD494 (src/BATTLE/BATTLE.PRG/40564.c)
/// Finds animation `animation` of `actor`: its table (the return value, a guest address; 0 when absent) and its frames (`frames`, NULL for an empty entry).
pub fn findAnimation(actor_: ?*anyopaque, animation: c_int, frames: [*c][*c]u16) callconv(.c) c_int {
    const actor: *align(1) v.D_800F4538_t = @ptrCast(actor_.?);
    var p: ?*Table = undefined;
    var idx: c_int = undefined;

    frames.* = null;

    if (animation == 0) {
        p = table(psx.get(actor.unk5D0));
        idx = 0;
    } else if (animation == 0xFF) {
        p = table(psx.get(actor.unk5DC[3]));
        if (actor.unk0.unkF == 0 or actor.unk6E6 == 2) {
            idx = v.g.D_800F49E4().*;
        } else {
            if (actor.unk5B1 == 0) {
                return 0;
            }
            idx = @as(c_int, actor.unk5B1) - 1; // a u8 above 0
            idx = idx *% 3 +% v.g.D_800F49E4().*;
        }
        if (idx >= actor.unk5B6) {
            return 0;
        }
    } else if (animation == 0xFE) {
        p = table(psx.get(actor.unk5DC[3]));
        if (actor.unk5B1 == 0) {
            return 0;
        }
        idx = @as(c_int, actor.unk5B1) - 1; // a u8 above 0
        idx = idx *% 3 +% 31;
        if (idx >= actor.unk5B6) {
            return 0;
        }
    } else if (animation >= 0xDC) {
        // unk5DC[D_800E8F2C]: the C does not check the index.
        p = table(psx.get(psx.at(&actor.unk5DC, v.g.D_800E8F2C().*).*));
        idx = animation -% 0xDC;
    } else if (animation >= 0xCA) {
        p = table(v.g.D_800F4768().*);
        idx = animation -% 0xCA;
    } else if (animation >= 100) {
        p = table(psx.get(actor.unk5D8));
        idx = animation -% 100;
        if (idx >= actor.unk5B5) {
            return 0;
        }
    } else {
        p = table(psx.get(actor.unk5D4));
        idx = animation;
        if (idx >= actor.unk5B4) {
            return 0;
        }
    }

    if (p == null) {
        return 0;
    }

    // unk4[idx]: a negative animation below 100 reads before the table, as the C does.
    idx = psx.at(psx.get(p.?.unk4).?, idx).*;
    v.g.D_800F49DC().* = psx.conv(u8, idx);

    if (idx == 0xFF) {
        frames.* = null;
    } else {
        frames.* = psx.at(&p.?.unk8, ((@as(c_int, actor.unk0.nBones) *% 4 +% 10) *% idx) >> 1);
    }
    return @bitCast(psx.addr(p.?));
}
