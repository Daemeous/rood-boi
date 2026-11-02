//! Draws a text box's speech-bubble frame (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

const BubbleFrame = *const fn (c_int, ?*const anyopaque, ?*const anyopaque, ?*const anyopaque, ?*const anyopaque) callconv(.c) void;

/// The bubble's projected points: 16-bit x, y pairs in the scratchpad, read back as words.
const points: [*]c_int = @ptrFromInt(0x1F800088);

fn px(k: c_int) c_int {
    return @as(*const [2]i16, @ptrCast(psx.at(points, k))).*[0];
}

fn py(k: c_int) c_int {
    return @as(*const [2]i16, @ptrCast(psx.at(points, k))).*[1];
}

fn link(prim: [*c]c_uint) c_uint {
    return (psx.addr(prim) << 8) >> 8;
}

/// orig: func_800CD3E4 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Box `index`'s bubble shape (D_800EB588, by its shape number): its outline points scaled into the box (the
/// tail part's points relative to its anchor), the outline as tiles (through the tail when shown), and its
/// textured faces with their shadows. ROOD_PC: the talk bubble's frame hook.
pub fn func_800CD3E4(index: c_int) callconv(.c) void {
    const scratch = v.g.vs_scratch();
    const frame_start = psx.get(scratch.unk0);
    const box = psx.at(v.g.textBoxes(), index);
    var x: c_int = box.unk24;
    var y: c_int = box.unk26;
    var w: c_int = box.unk28;
    var h: c_int = box.unk2A;
    var verts: ?[*]align(1) u16 = @ptrCast(psx.at(v.g.D_800EB588(), psx.getBits(&box.unk0, "unk0_8")).*);
    // D_800F51B8 + index * 4: computed before the shape test, read only after it.
    const ot_base: usize = if (v.g.D_800F51B8().*) |p| @intFromPtr(p) else 0;
    const ot: [*c]c_uint = @ptrFromInt(ot_base +% @as(usize, @bitCast(@as(isize, index) * 16)));
    if (verts == null) {
        return;
    }
    var out: [*]i16 = @ptrFromInt(0x1F800088);
    var flip: c_int = psx.getBits(&box.unk0, "unk0_6");
    const shape: [*]u8 = @ptrCast(verts.?);
    const tail: c_int = psx.getBits(&box.unk0, "unk0_12");
    var part: c_uint = @bitCast(psx.at(@as([*]align(1) const c_int, @ptrCast(shape)), tail).*);
    verts = @ptrCast(shape + (@as(usize, shape[1]) + 1) * 4);
    const count: u8 = shape[2];
    const header: c_int = @as(*align(1) const c_int, @ptrCast(shape)).*;
    var i: c_int = 0;
    var v_: c_int = undefined;
    var n: c_int = undefined;
    var k: c_int = undefined;
    while (i < count) : (i += 1) {
        v_ = c.func_800CD3A0(verts.?[@intCast(i)], flip);
        out[0] = psx.conv(i16, x +% (((v_ & 0xFF) *% w) >> 8));
        out[1] = psx.conv(i16, y +% (((v_ >> 8) *% h) >> 8));
        out += 2;
    }
    if (psx.conv(c_uint, tail - 1) < @as(c_uint, @bitCast((header >> 8) & 0xFF))) {
        v_ = c.func_800CD3A0(verts.?[part & 0xFF], flip);
        while (i < (header & 0xFF)) : (i += 1) {
            n = c.func_800CD3A0(verts.?[@intCast(i)], flip);
            k = n >> 8;
            n &= 0xFF;
            if (n >= 0x80) {
                n -= 0x100;
            }
            if (k >= 0x80) {
                k -= 0x100;
            }
            out[0] = psx.conv(i16, x +% (((n +% (v_ & 0xFF)) *% w) >> 8));
            out[1] = psx.conv(i16, y +% (((k +% (v_ >> 8)) *% h) >> 8));
            out += 2;
        }
    } else {
        part = 0xFFFF;
    }
    verts = verts.? + @as(usize, @intCast(header & 0xFF));
    i = (header >> 16) & 0xFF;
    n = 0;
    while (n < i) {
        if (n == (part & 0xFF)) {
            v_ = 0;
            k = 0;
            while (k < tail) : (k += 1) {
                v_ += shape[@intCast(k * 4 + 2)];
            }
            k = 0;
            while (k < shape[@intCast(tail * 4 + 2)]) : (k += 1) {
                c.vs_battle_addTile(ot + 2, 0x40303030, psx.at(points, n).*, psx.at(points, v_ + k).*);
                n = v_ + k;
            }
            v_ = n;
            n = @intCast((part >> 8) & 0xFF);
            c.vs_battle_addTile(ot + 2, 0x40303030, psx.at(points, n).*, psx.at(points, v_).*);
        }
        v_ = @rem(n + 1, i);
        c.vs_battle_addTile(ot + 2, 0x40303030, psx.at(points, n).*, psx.at(points, v_).*);
        n = n + 1;
    }
    c.vs_battle_insertTpage(@bitCast(@as(c_uint, 0xE2000000)), ot + 2);
    var prim: [*c]c_uint = @ptrCast(@alignCast(psx.get(scratch.unk0)));
    flip = box.brightness;
    flip = flip | (flip << 8) | (flip << 16);
    n = 0;
    while (n <= shape[1]) : (n += 1) {
        k = 0;
        while (k < shape[@intCast(n * 4 + 3)]) : (k += 1) {
            const face: [*]const u8 = @ptrCast(verts.?);
            if (n != 0 and n != tail) {
                verts = verts.? + 2;
                continue;
            }
            v_ = face[0];
            i = face[1];
            w = face[2];
            h = face[3];
            verts = verts.? + 2;
            x = px(v_);
            if (px(i) < x) x = px(i);
            if (px(w) < x) x = px(w);
            y = py(v_);
            if (py(i) < y) y = py(i);
            if (py(w) < y) y = py(w);
            const quad: c_int = @as(c_int, @intFromBool(h != 0xFF)) * 2;
            if (quad != 0) {
                if (px(h) < x) x = px(h);
                if (py(h) < y) y = py(h);
            } else {
                h = 0;
            }
            prim[0] = (ot[2] & 0xFFFFFF) | @as(c_uint, @bitCast((quad + 7) << 24));
            prim[1] = @bitCast(flip | ((quad + 9) << 26));
            x = (x >> 6) << 6;
            y = (y >> 6) << 6;
            prim[2] = @bitCast(psx.at(points, v_).*);
            prim[3] = @bitCast(((px(v_) - x) & 0xFF) | (((py(v_) - y) << 8) & 0xFF00) | 0x373E0000);
            prim[4] = @bitCast(psx.at(points, i).*);
            prim[5] = @bitCast(((px(i) - x) & 0xFF) | (((py(i) - y) << 8) & 0xFF00) | 0x170000);
            prim[6] = @bitCast(psx.at(points, w).*);
            prim[7] = @bitCast(((px(w) - x) & 0xFF) | (((py(w) - y) << 8) & 0xFF00));
            prim[8] = @bitCast(psx.at(points, h).*);
            prim[9] = @bitCast(((px(h) - x) & 0xFF) | (((py(h) - y) << 8) & 0xFF00));
            ot[2] = link(prim);
            prim += @as(usize, @intCast(quad + 8));
            prim[0] = (ot[1] & 0xFFFFFF) | 0x0C000000;
            prim[1] = @bitCast((quad + 8) << 26);
            prim[2] = @bitCast(((px(v_) + 2) & 0xFFFF) | ((py(v_) + 2) << 16));
            prim[3] = @bitCast(((px(i) + 2) & 0xFFFF) | ((py(i) + 2) << 16));
            prim[4] = @bitCast(((px(w) + 2) & 0xFFFF) | ((py(w) + 2) << 16));
            if (quad != 0) {
                prim[5] = @bitCast(((px(h) + 2) & 0xFFFF) | ((py(h) + 2) << 16));
            } else {
                prim[5] = 0;
            }
            prim[6] = 0x48000000;
            prim[7] = prim[2];
            prim[8] = prim[3];
            prim[9] = prim[@intCast(quad + 3)];
            prim[10] = prim[4];
            prim[11] = prim[2];
            prim[12] = 0x55555555;
            ot[1] = link(prim);
            prim += 13;
        }
    }
    psx.put(&scratch.unk0, @as(?*anyopaque, @ptrCast(prim)));
    c.vs_battle_addTile(ot + 2, @bitCast(@as(c_uint, 0xE1000017)), @bitCast(@as(c_uint, 0xE2000318)), 0);
    // ROOD_PC: the talk bubble's frame.
    if (psx.weak(BubbleFrame, "pc_bubble_frame")) |hook| hook(index, box, shape, frame_start, psx.get(scratch.unk0));
}
