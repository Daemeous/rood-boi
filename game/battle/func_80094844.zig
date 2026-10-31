//! Places a display label near the labels already placed this frame, nudging it to a free spot on overlap
//! (src/BATTLE/BATTLE.PRG/2842C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_2842C");

fn abs(x: c_int) c_int {
    return if (x >= 0) x else -%x;
}

fn spots() [*]align(1) v.DVECTOR {
    return @ptrCast(v.g.D_800F1CBC().*.?);
}

fn count() c_int {
    return @as(c_int, v.g.D_800F227C().*);
}

/// orig: func_80094844 (src/BATTLE/BATTLE.PRG/2842C.c)
/// Places a label at `pos`, moves it to a free spot if it overlaps a placed label, clamps it and records it.
pub fn func_80094844(pos: [*c]c_short, label: ?*v.D_800F1BAC_t) callconv(.c) void {
    const lbl = label.?;
    var i: c_int = 0;
    var j: c_int = 0;
    var dx: c_int = 0;
    var dy: c_int = 0;
    var x: c_int = 0;
    var y: c_int = 0;

    if (lbl.unk10 == 0x1000 and lbl.unk12 == 0x1000) {
        lbl.unk10 = 0;
        lbl.unk12 = 0;
        if (count() > 0) {
            i = 0;
            while (i < count()) : (i += 1) {
                x = @as(c_int, pos[0]) -% @as(c_int, psx.at(spots(), i).vx);
                y = @as(c_int, pos[1]) -% @as(c_int, psx.at(spots(), i).vy);
                if (abs(x) < 24 and abs(y) < 10) {
                    break;
                }
            }
            if (i != count()) {
                // i is reused as the "free spot found" flag
                i = 0;
                done: {
                    dy = 0;
                    while (dy < 48) : (dy += 8) {
                        dx = 0;
                        while (dx < 32) : (dx += 8) {
                            j = 0;
                            while (j < count()) : (j += 1) {
                                y = @as(c_int, pos[1]) +% dy -% @as(c_int, psx.at(spots(), j).vy);
                                if (abs(y) < 10) {
                                    x = @as(c_int, pos[0]) +% dx -% @as(c_int, psx.at(spots(), j).vx);
                                    if (abs(x) < 24) {
                                        break;
                                    }
                                }
                            }
                            if (j == count()) {
                                i = 1;
                                break :done;
                            }
                            j = 0;
                            while (j < count()) : (j += 1) {
                                y = @as(c_int, pos[1]) +% dy -% @as(c_int, psx.at(spots(), j).vy);
                                if (abs(y) < 10) {
                                    x = @as(c_int, pos[0]) -% dx -% @as(c_int, psx.at(spots(), j).vx);
                                    if (abs(x) < 24) {
                                        break;
                                    }
                                }
                            }
                            if (j == count()) {
                                i = 1;
                                dx = -%dx;
                                break :done;
                            }
                        }
                    }
                }
                if (i != 0) {
                    lbl.unk10 = psx.conv(i16, dx);
                    lbl.unk12 = psx.conv(i16, dy);
                }
            }
        }
    }
    pos[0] = psx.conv(c_short, @as(c_int, pos[0]) +% @as(c_int, lbl.unk10));
    pos[1] = psx.conv(c_short, @as(c_int, pos[1]) +% @as(c_int, lbl.unk12));
    const slot = psx.at(spots(), count());
    slot.vx = pos[0];
    slot.vy = pos[1];
    if (pos[0] < 32) {
        pos[0] = 32;
    }
    if (pos[0] > 288) {
        pos[0] = 288;
    }
    if (pos[1] > 200) {
        pos[1] = 200;
    }
    v.g.D_800F227C().* +%= 1;
}
