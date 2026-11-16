//! Draws a text box's speech-bubble frame (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const pad = @import("common").pad;
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");
const sprite_item = common.sprite_item;

const BubbleFace = *const fn ([*c]c_uint, c_int, c_int, [*]const c_int, [*]const c_int) callconv(.c) void;
const BubbleShadow = *const fn ([*c]c_uint, c_int, [*]const c_int) callconv(.c) void;
const BubbleFrame = *const fn (c_int, *const c_uint, *const [4]i16, ?*const anyopaque, ?*const anyopaque, ?*const anyopaque) callconv(.c) void;

/// The bubble's projected points: 16-bit x, y pairs in the scratchpad, read back as words.
const points: [*]c_int = @ptrCast(pad.ptr(c_int, 0x88));

fn px(k: c_int) c_int {
    return @as(*const [2]i16, @ptrCast(psx.at(points, k))).*[0];
}

fn py(k: c_int) c_int {
    return @as(*const [2]i16, @ptrCast(psx.at(points, k))).*[1];
}

/// orig: vs_menu_drawTextBoxFrame (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Box `index`'s bubble shape (vs_event_textBoxFrameStyles, by its shape number): its outline points scaled into the box (the
/// tail part's points relative to its anchor), the outline as tiles (through the tail when shown), and its
/// textured faces with their shadows. ROOD_PC: the talk bubble's frame hook.
pub fn vs_menu_drawTextBoxFrame(index: c_int) callconv(.c) void {
    const scratch = v.g.vs_scratch();
    const frame_start = psx.get(scratch.unk0);
    const box = psx.at(v.g.textBoxes(), index);
    var x: c_int = box.unk24;
    var y: c_int = box.unk26;
    var w: c_int = box.unk28;
    var h: c_int = box.unk2A;
    var verts: ?[*]align(1) u16 = @ptrCast(psx.at(v.g.vs_event_textBoxFrameStyles(), psx.getBits(&box.flags, "unk0_8")).*);
    // vs_menu_activeTextBoxOrderingTable + index * 4: computed before the shape test, read only after it.
    const ot_base: usize = if (v.g.vs_menu_activeTextBoxOrderingTable().*) |p| @intFromPtr(p) else 0;
    const ot: [*c]c_uint = @ptrFromInt(ot_base +% @as(usize, @bitCast(@as(isize, index) * 16)));
    if (verts == null) {
        return;
    }
    var out: [*]i16 = @ptrFromInt(pad.addr(0x88));
    var flip: c_int = psx.getBits(&box.flags, "unk0_6");
    const shape: [*]u8 = @ptrCast(verts.?);
    const tail: c_int = psx.getBits(&box.flags, "unk0_12");
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
                sprite_item.lineFlat(ot + 2, false, 0x303030, psx.at(points, n).*, psx.at(points, v_ + k).*);
                n = v_ + k;
            }
            v_ = n;
            n = @intCast((part >> 8) & 0xFF);
            sprite_item.lineFlat(ot + 2, false, 0x303030, psx.at(points, n).*, psx.at(points, v_).*);
        }
        v_ = @rem(n + 1, i);
        sprite_item.lineFlat(ot + 2, false, 0x303030, psx.at(points, n).*, psx.at(points, v_).*);
        n = n + 1;
    }
    sprite_item.drawModeWindow(ot + 2, -1, 0); // the texture window reset (GP0 E2) alone
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
            x = (x >> 6) << 6;
            y = (y >> 6) << 6;
            // The face (a textured polygon, its texels the corners' offsets from the top left of the 64 grid) and its shadow
            // (two pixels over and down: a black polygon and its outline), as draw items.
            const corners = [4]c_int{ v_, i, w, h };
            var xy: [4]c_int = undefined;
            var uv: [4]c_int = undefined;
            var shadow: [4]c_int = undefined;
            var m: usize = 0;
            while (m < 4) : (m += 1) {
                xy[m] = psx.at(points, corners[m]).*;
                uv[m] = ((px(corners[m]) - x) & 0xFF) | (((py(corners[m]) - y) << 8) & 0xFF00);
                shadow[m] = ((px(corners[m]) + 2) & 0xFFFF) | ((py(corners[m]) + 2) << 16);
            }
            const verts_n: c_int = if (quad != 0) 4 else 3;
            if (psx.weak(BubbleFace, "vs_battle_bubbleFace")) |face_item| face_item(ot + 2, verts_n, flip, &xy, &uv);
            if (psx.weak(BubbleShadow, "vs_battle_bubbleShadow")) |shadow_item| shadow_item(ot + 1, verts_n, &shadow);
        }
    }
    sprite_item.drawModeWindow(ot + 2, 0x17, 0x318); // the draw mode (GP0 E1) and the 64-texel tile window (E2) the frame leaves set
    // ROOD_PC: the talk bubble's frame.
    if (psx.weak(BubbleFrame, "pc_bubble_frame")) |hook| hook(index, @ptrCast(&box.flags), @ptrCast(&box.unk24), shape, frame_start, psx.get(scratch.unk0));
}
