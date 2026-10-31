//! Object animation step: rotation, then the slide, fall, settle and tick modes
//! (src/BATTLE/BATTLE.PRG/44F14.c).
const psx = @import("common").psx;
const v = @import("views_44F14");
const c = @import("c_44F14");

/// Stores `value` into an i16 field, as C's conversion does.
fn setI16(p: *i16, value: c_int) void {
    p.* = psx.conv(i16, value);
}

/// `field += delta` on an i16 field.
fn addI16(p: *i16, delta: c_int) void {
    p.* = psx.conv(i16, @as(c_int, p.*) +% delta);
}

/// Slide settle on one axis: the last small velocity is added to the position and zeroed.
fn settleAxis(pos: *i16, vel: *i16) void {
    if (@as(c_int, vel.*) < 0) {
        if (@as(c_int, vel.*) >= -1) {
            addI16(pos, @as(c_int, vel.*));
            vel.* = 0;
        }
    } else if (@as(c_int, vel.*) < 2) {
        addI16(pos, @as(c_int, vel.*));
        vel.* = 0;
    }
}

/// orig: func_800B0908 (src/BATTLE/BATTLE.PRG/44F14.c)
/// Advances one object by a tick: its rotation, then the motion of its mode, and its children when it falls.
pub fn func_800B0908(object: ?*v.objectAnimationState, _: c_int) callconv(.c) void {
    const obj = object.?;
    const scratch: *v.actorTransformScratch = @ptrFromInt(0x1F80035C);
    // *(int*)&object->material: the four bytes from material on.
    const mat4: *align(1) c_int = psx.byteAt(c_int, obj, @offsetOf(v.objectAnimationState, "material"));
    var step: c_int = 0;
    var other: c_int = 0;

    if (obj.delay != 0) {
        obj.delay -%= 1;
        return;
    }

    if (obj.rotationTicks != 0) {
        step = @divTrunc(@as(c_int, obj.drx), @as(c_int, obj.rotationTicks));
        addI16(&obj.drx, 0 -% step);
        setI16(&obj.rx, (@as(c_int, obj.rx) +% step) & 0xFFF);

        step = @divTrunc(@as(c_int, obj.dry), @as(c_int, obj.rotationTicks));
        addI16(&obj.dry, 0 -% step);
        setI16(&obj.ry, (@as(c_int, obj.ry) +% step) & 0xFFF);

        step = @divTrunc(@as(c_int, obj.drz), @as(c_int, obj.rotationTicks));
        addI16(&obj.drz, 0 -% step);
        setI16(&obj.rz, (@as(c_int, obj.rz) +% step) & 0xFFF);

        obj.rotationTicks -%= 1;
    }

    const mode: c_int = @as(c_int, obj.mode);
    if (mode == 0) return;

    // The C's gotos (fall, children, animate) as entries into one state loop.
    const Entry = enum { dispatch, fall, children, animate };
    var entry: Entry = .dispatch;
    while (true) switch (entry) {
        .dispatch => {
            if (mode == 0xFF) {
                if (obj.dx == 0 and obj.dz == 0) {
                    obj.frame = 0;
                    entry = .fall;
                    continue;
                }
                obj.frame = 1;
                step = @divTrunc(@as(c_int, obj.dx), 2);
                addI16(&obj.dx, 0 -% step);
                addI16(&obj.position.vx, step);
                settleAxis(&obj.position.vx, &obj.dx);
                step = @divTrunc(@as(c_int, obj.dz), 2);
                addI16(&obj.dz, 0 -% step);
                addI16(&obj.position.vz, step);
                settleAxis(&obj.position.vz, &obj.dz);
                return;
            } else if (mode == 0xFE) {
                if ((@as(c_int, obj.position.vx) & 0x7F) == 0x40 and (@as(c_int, obj.position.vz) & 0x7F) == 0x40) {
                    scratch.rotation.vx = obj.position.vx;
                    scratch.rotation.vz = obj.position.vz;
                    step = @as(c_int, obj.orientation);
                    other = 128;
                    if (step < 2) other = -128;
                    if ((step & 1) != 0) {
                        addI16(&scratch.rotation.vx, other);
                    } else {
                        addI16(&scratch.rotation.vz, other);
                    }
                    if (c.func_800A3DB4(@as(c_int, scratch.rotation.vx), @as(c_int, scratch.rotation.vz), @as(c_int, obj.position.vy)) != 0 and
                        c.func_800A6EE8(psx.cptr(c.SVECTOR, &obj.position), 0, 0, 1) == @as(c_int, obj.position.vy))
                    {
                        obj.tileX = 0xFF;
                        obj.tileZ = 0xFF;
                    } else {
                        entry = .fall;
                        continue;
                    }
                }
                addI16(&obj.position.vx, @as(c_int, obj.dx));
                addI16(&obj.position.vz, @as(c_int, obj.dz));
                return;
            } else if (mode == 0xFD) {
                if (c.func_800B101C(@ptrCast(obj)) == 0) {
                    c.func_800E6898(obj);
                    c.func_800E6898(obj);
                    c.func_800E4C28(@as(c_int, obj.tileX), @as(c_int, obj.tileZ));
                }
                return;
            } else if (mode == 0xFC) {
                entry = .fall;
                continue;
            } else if (mode == 0xFB or mode == 0xF7 or mode == 0xF8) {
                entry = .animate;
                continue;
            } else if (mode == 0xFA) {
                _ = c.func_800B101C(@ptrCast(obj));
                entry = .children;
                continue;
            } else if (mode == 0xF9) {
                if (c.func_800B101C(@ptrCast(obj)) != 0) return;
                entry = .fall;
                continue;
            }

            // Other modes: the object moves on its velocity and its mode counts down.
            step = @divTrunc(@as(c_int, obj.dx), @as(c_int, obj.mode));
            addI16(&obj.dx, 0 -% step);
            addI16(&obj.position.vx, step);
            step = @divTrunc(@as(c_int, obj.dy), @as(c_int, obj.mode));
            addI16(&obj.dy, 0 -% step);
            addI16(&obj.position.vy, step);
            step = @divTrunc(@as(c_int, obj.dz), @as(c_int, obj.mode));
            addI16(&obj.dz, 0 -% step);
            addI16(&obj.position.vz, step);
            obj.mode -%= 1;
            if (obj.mode == 0 and obj.parent == 0xFF) {
                entry = .fall;
                continue;
            }
            return;
        },
        .fall => {
            obj.tileX = psx.conv(u8, @divTrunc(@as(c_int, obj.position.vx), 128));
            obj.tileZ = psx.conv(u8, @divTrunc(@as(c_int, obj.position.vz), 128));
            step = c.func_800A6EE8(psx.cptr(c.SVECTOR, &obj.position), 0, 0, 2);
            if (@as(c_int, obj.position.vy) < step) {
                addI16(&obj.dy, 3);
                addI16(&obj.position.vy, @as(c_int, obj.dy));
                if (@as(c_int, obj.position.vy) < step) {
                    obj.mode = 0xFC;
                    entry = .children;
                    continue;
                }
            }
            {
                const previousY: c_int = psx.conv(u16, obj.position.vy);
                obj.position.vy = psx.conv(i16, step);
                addI16(&obj.dy, step -% previousY);
            }
            other = @as(c_int, obj.material) & 7;
            if (other >= 5) {
                mat4.* &= ~@as(c_int, 0x30);
                other = c.func_800A91DC(@as(c_int, obj.tileX), @as(c_int, obj.tileZ), 1);
                if (other != 0) {
                    const landed = psx.at(v.g.D_800F45E0(), other).*.?;
                    other = @as(c_int, psx.byteAt(u8, landed, 0x16C).*) & 7;
                    if (other >= 5) {
                        if (other == (@as(c_int, obj.material) & 7)) {
                            obj.mode = 0xFD;
                            obj.frame = 0;
                            mat4.* = (mat4.* & ~@as(c_int, 0x30)) | 0x20;
                            c.vs_main_panSfx(0x7E, 0x30, psx.cptr(c.SVECTOR, &obj.position));
                        } else {
                            mat4.* = (mat4.* & ~@as(c_int, 0x30)) | 0x10;
                            c.vs_main_panSfx(0x7E, 0x31, psx.cptr(c.SVECTOR, &obj.position));
                            obj.mode = 0;
                            c.func_800E6898(obj);
                            c.func_800E4C28(@as(c_int, obj.tileX), @as(c_int, obj.tileZ));
                        }
                        entry = .children;
                        continue;
                    }
                }
            }
            obj.tileX = psx.conv(u8, @divTrunc(@as(c_int, obj.position.vx), 128));
            obj.tileZ = psx.conv(u8, @divTrunc(@as(c_int, obj.position.vz), 128));
            c.func_800E6898(obj);
            c.func_800E4C28(@as(c_int, obj.tileX), @as(c_int, obj.tileZ));
            if ((obj.material & 7) == 1) {
                obj.durability -%= 1;
                if (obj.durability == 0) {
                    obj.mode = 0xF8;
                    obj.frame = 0;
                    return;
                }
            }
            step = @as(c_int, obj.mode);
            if (step == 0xFE) {
                c.vs_main_panSfx(0x7E, 0x30, psx.cptr(c.SVECTOR, &obj.position));
                obj.mode = 0xFB;
                obj.frame = 0;
            } else if (step == 0xFF or step == 0xF9) {
                obj.mode = 0;
            } else {
                c.vs_main_panSfx(0x7E, 0x30, psx.cptr(c.SVECTOR, &obj.position));
                obj.mode = 0xFA;
                obj.frame = 0;
            }
            entry = .children;
            continue;
        },
        .children => {
            step = 0;
            while (step < 16) : (step +%= 1) {
                const child: ?*v.objectAnimationState = @ptrCast(psx.at(v.g.D_800F45E0(), step).*);
                if (child) |ch| {
                    if ((ch.flags & 1) == 0 and ch.parentObject == obj.id) {
                        addI16(&ch.position.vy, @as(c_int, obj.dy));
                        if (obj.mode == 0) ch.parentObject = 0xFF;
                    }
                }
            }
            return;
        },
        .animate => {
            _ = c.func_800B101C(@ptrCast(obj));
            return;
        },
    };
}
