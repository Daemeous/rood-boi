//! Draws one segment of an effect trail (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800CDCBC (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Projects the segment's four points (RotAverage4) and, inside the depth range, adds a quad: Gouraud from table D_800EC308 (config 6, through func_800CF988), else textured from D_800EC2E8, the head and tail brightnesses scaling the colours.
pub fn func_800CDCBC(trail: ?*v.effectTrailState, head: c_int, tail: c_int) callconv(.c) void {
    const t = trail.?;
    var xy: [4]c_int = undefined;
    const depth = c.RotAverage4(@ptrCast(&t.previousFirst), @ptrCast(&t.currentFirst), @ptrCast(&t.previousSecond), @ptrCast(&t.currentSecond), &xy[0], &xy[1], &xy[2], &xy[3], &t.projectionDepth, &t.projectionFlags);
    if (psx.conv(c_uint, depth -% 17) >= 2031) {
        return;
    }
    const scratch = v.g.vs_scratch();
    const p: *v.TrailPacket = @ptrCast(@alignCast(psx.get(scratch.unk0).?));
    psx.put(&scratch.unk0, @as(*anyopaque, @ptrFromInt(@intFromPtr(p) + @sizeOf(v.TrailPacket))));
    p.tag = (p.tag & 0xFFFFFF) | (14 << 24); // setlen(p, 14)
    p.poly.code = 0x3C;
    p.poly.tag = 0xE1000000;
    p.endPage = 0xE1000200;
    @as(*align(2) c_int, @ptrCast(&p.poly.x0)).* = xy[0];
    @as(*align(2) c_int, @ptrCast(&p.poly.x1)).* = xy[1];
    @as(*align(2) c_int, @ptrCast(&p.poly.x2)).* = xy[2];
    @as(*align(2) c_int, @ptrCast(&p.poly.x3)).* = xy[3];
    const color: usize = psx.getBits(&t.config.bits, "color");
    if ((t.config.value & 0xF00000) == 0x600000) {
        const rgb = v.g.D_800EC308()[color];
        p.poly.r1 = psx.conv(u8, @as(c_int, rgb.r) *% head -% 128);
        p.poly.r0 = p.poly.r1;
        p.poly.g1 = psx.conv(u8, @as(c_int, v.g.D_800EC308()[color].@"g") *% head -% 128);
        p.poly.g0 = p.poly.g1;
        p.poly.b1 = psx.conv(u8, @as(c_int, v.g.D_800EC308()[color].b) *% head -% 128);
        p.poly.b0 = p.poly.b1;
        p.poly.r3 = psx.conv(u8, @as(c_int, v.g.D_800EC308()[color].r) *% tail -% 128);
        p.poly.r2 = p.poly.r3;
        p.poly.g3 = psx.conv(u8, @as(c_int, v.g.D_800EC308()[color].@"g") *% tail -% 128);
        p.poly.g2 = p.poly.g3;
        p.poly.b3 = psx.conv(u8, @as(c_int, v.g.D_800EC308()[color].b) *% tail -% 128);
        p.poly.b2 = p.poly.b3;
        c.func_800CF988(@ptrCast(p), head, head, 0);
        // addPrim(ot + depth, p): p's tag links to the entry's old successor, the entry to p (24-bit links).
        const entry = &@as([*]u32, @ptrCast(@alignCast(psx.get(scratch.unk4).?)))[@intCast(depth)];
        p.tag = (p.tag & 0xFF000000) | (entry.* & 0xFFFFFF);
        entry.* = (entry.* & 0xFF000000) | (psx.addr(p) & 0xFFFFFF);
    } else {
        p.poly.code = 0x3E;
        var uv: u16 = 0x58;
        if ((t.config.value & 0xF0000) != 0x70000) {
            uv = 0x38;
        }
        p.poly.tpage = uv;
        p.poly.clut = 0x3C30;
        const texture: c_uint = psx.getBits(&t.config.bits, "texture");
        const column = psx.conv(u8, texture % 3);
        const row = psx.conv(u8, texture / 3);
        uv = psx.conv(u16, @as(c_int, column) * 2560 + @as(c_int, row) * 40);
        @as(*align(1) u16, @ptrCast(&p.poly.u0)).* = uv +% 0x4868;
        @as(*align(1) u16, @ptrCast(&p.poly.u2)).* = uv +% 0x4887;
        @as(*align(1) u16, @ptrCast(&p.poly.u1)).* = uv +% 0x4F68;
        @as(*align(1) u16, @ptrCast(&p.poly.u3)).* = uv +% 0x4F87;
        const table = v.g.D_800EC2E8();
        p.poly.r1 = psx.conv(u8, @divTrunc(@as(c_int, table[color].r) *% head, 8));
        p.poly.r0 = p.poly.r1;
        p.poly.g1 = psx.conv(u8, @divTrunc(@as(c_int, table[color].@"g") *% head, 8));
        p.poly.g0 = p.poly.g1;
        p.poly.b1 = psx.conv(u8, @divTrunc(@as(c_int, table[color].b) *% head, 8));
        p.poly.b0 = p.poly.b1;
        p.poly.r3 = psx.conv(u8, @divTrunc(@as(c_int, table[color].r) *% tail, 8));
        p.poly.r2 = p.poly.r3;
        p.poly.g3 = psx.conv(u8, @divTrunc(@as(c_int, table[color].@"g") *% tail, 8));
        p.poly.g2 = p.poly.g3;
        p.poly.b3 = psx.conv(u8, @divTrunc(@as(c_int, table[color].b) *% tail, 8));
        p.poly.b2 = p.poly.b3;
        // addPrim(ot + depth, p): p's tag links to the entry's old successor, the entry to p (24-bit links).
        const entry = &@as([*]u32, @ptrCast(@alignCast(psx.get(scratch.unk4).?)))[@intCast(depth)];
        p.tag = (p.tag & 0xFF000000) | (entry.* & 0xFFFFFF);
        entry.* = (entry.* & 0xFF000000) | (psx.addr(p) & 0xFFFFFF);
    }
}
