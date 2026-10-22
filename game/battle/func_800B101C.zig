//! Object motion keyframes: advances one object's scripted motion by a frame
//! (src/BATTLE/BATTLE.PRG/44F14.c).
const psx = @import("common").psx;
const v = @import("views_44F14");
const c = @import("c_44F14");

/// orig: func_800B101C (src/BATTLE/BATTLE.PRG/44F14.c)
/// Steps one object's motion by a frame: returns 1 while it runs, 0 once it completes.
pub fn func_800B101C(object: ?*v.objectMotionContext) callconv(.c) c_int {
    const Step = enum { next, complete, done };
    const o = object.?;
    var frame: c_int = o.frame;
    if (frame == 0) frame = 1;
    const value: c_int = o.mode;

    const step: Step = blk: {
        if (value == 0xFD) {
            o.y = psx.conv(i16, @as(c_int, o.y) +% @as(c_int, @as(i8, @bitCast(psx.at(v.g.D_800E91AC(), frame).*))));
            o.rotationX = psx.conv(i16, @as(c_int, o.rotationX) +% @as(c_int, @as(i8, @bitCast(psx.at(v.g.D_800E91BC(), frame).*))));
            o.rotationY = psx.conv(i16, @as(c_int, o.rotationY) +% @as(c_int, @as(i8, @bitCast(psx.at(v.g.D_800E91CC(), frame).*))));
            o.rotationZ = psx.conv(i16, @as(c_int, o.rotationZ) +% @as(c_int, @as(i8, @bitCast(psx.at(v.g.D_800E91DC(), frame).*))));
            if (frame == 14) break :blk .complete;
            break :blk .next;
        } else if (value == 0xFB) {
            const orientation: c_int = o.orientation;
            o.z = psx.conv(i16, @as(c_int, o.z) +% @as(c_int, @as(i8, @bitCast(psx.at(v.g.D_800E91F0(), frame).*))));
            var value2: c_int = @as(i8, @bitCast(psx.at(v.g.D_800E91FC(), frame).*));
            if (orientation == 0 or orientation == 3) value2 = -%value2;
            if ((orientation & 1) != 0) {
                o.rotationZ = psx.conv(i16, @as(c_int, o.rotationZ) +% value2);
            } else {
                o.rotationX = psx.conv(i16, @as(c_int, o.rotationX) +% value2);
            }
            if (frame == 9) break :blk .complete;
            break :blk .next;
        } else if (value == 0xFA) {
            o.y = psx.conv(i16, @as(c_int, o.y) +% @as(c_int, @as(i8, @bitCast(psx.at(v.g.D_800E91EC(), frame).*))));
            o.deltaY = psx.conv(i16, @as(i8, @bitCast(psx.at(v.g.D_800E91EC(), frame).*)));
            if (frame == 3) break :blk .complete;
            break :blk .next;
        } else if (value == 0xF9) {
            const dy: c_int = @as(i8, @bitCast(psx.at(v.g.D_800E9208(), frame).*));
            o.y = psx.conv(i16, @as(c_int, o.y) +% dy);
            var value3: c_int = @as(i8, @bitCast(psx.at(v.g.D_800E9218(), frame).*));
            const orientation: c_int = o.orientation;
            var value2: c_int = psx.at(v.g.D_800E9228(), frame).*;
            if (orientation >= 2) value3 = -%value3;
            if (psx.conv(c_uint, orientation -% 1) < 2) value2 = -%value2;
            if ((orientation & 1) != 0) {
                o.x = psx.conv(i16, @as(c_int, o.x) +% value3);
                o.rotationZ = psx.conv(i16, @as(c_int, o.rotationZ) +% value2);
            } else {
                o.z = psx.conv(i16, @as(c_int, o.z) +% value3);
                o.rotationX = psx.conv(i16, @as(c_int, o.rotationX) +% value2);
            }
            if (frame == 14) {
                _ = c.func_800A0104(o.objectId, o.material);
                o.mode = 0;
                break :blk .done;
            }
            break :blk .next;
        } else if (value == 0xF8) {
            if (frame >= 9) {
                o.y = psx.conv(i16, @as(c_int, o.y) +% 3);
                if (frame == 9) {
                    c.vs_main_memcpy(&o.state, &o.savedState, 32);
                    o.flags = (o.flags & ~@as(u32, 0xF00)) | 0xC00;
                }
                if (frame == 40) {
                    _ = c.func_80099D6C(o.objectId);
                    o.mode = 0;
                    break :blk .done;
                }
            }
            break :blk .next;
        } else if (value == 0xF7) {
            o.y = psx.conv(i16, @as(c_int, o.y) +% @as(c_int, @as(i8, @bitCast(psx.at(v.g.D_800E9238(), frame).*))));
            o.scaleX = psx.conv(i16, @as(i8, @bitCast(psx.at(v.g.D_800E9248(), frame).*)));
            o.scaleY = psx.conv(i16, @as(i8, @bitCast(psx.at(v.g.D_800E9258(), frame).*)));
            o.scaleZ = psx.conv(i16, @as(i8, @bitCast(psx.at(v.g.D_800E9268(), frame).*)));
            if (frame == 14) break :blk .complete;
            break :blk .next;
        }
        break :blk .next;
    };

    switch (step) {
        .next => {
            o.frame = psx.conv(u8, frame +% 1);
            return 1;
        },
        .complete => {
            o.mode = 0;
            return 0;
        },
        .done => return 0,
    }
}
