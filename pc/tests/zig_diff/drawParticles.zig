//! zig_diff spec: func_800D6E44 -> draw_particles.drawParticles (D2: pc/src/effect_particles.c).
//! Inputs: a list of 0 to 3 particle nodes in the arena, each with a sprite
//! script of the four valid command kinds (offset, frame, restart, stop;
//! an unknown kind loops forever after its pc_unimplemented stub, as the C
//! does, so none is generated) started at any of four offsets, a frame
//! table and tile data, a timer (0 to a few), flags choosing the attached
//! matrix (0x20000), the behind-camera tests (0x80000) and the tilt block
//! (0x10000), the depth-mode override in the packed word (modes 0 to 8),
//! colour curves (absent, or 1..32 with sizes 0..40 and a 256-byte sample
//! array), the P-file block, the parent with its 16 matrices, the packet
//! and ordering-table pointers in vs_scratch (the OT padded for the
//! negative slots), the near clip and the projection distance. The GTE and
//! libgte are recording stubs (MFC2 returns depths below 0x2000), as are
//! the three native-particle hooks (the end hook returns null or the packet).
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_effect_particles");
const psx = @import("common").psx;
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").drawParticles));

pub const classes = [_][:0]const u8{ "empty", "plain", "attached", "attached-behind", "unattached-behind", "tilted", "curves", "depth-modes" };

var scenario: usize = 0;
var end_null: bool = false;

pub fn prepare(ctx: *zd.Ctx) Args {
    scenario = ctx.range(usize, 0, classes.len - 1);
    end_null = ctx.chance(2);
    const pfile = ctx.alloc(v.D_800F569C_t);
    const block = ctx.allocBytes(4 + 40, 16);
    std.mem.writeInt(u16, block[0..2], 32, .little);
    std.mem.writeInt(u16, block[2..4], 4, .little);
    for (block[4..44]) |*size| size.* = ctx.edge(u8, &.{ 0, 1, 3, 40, ctx.range(u8, 1, 40) });
    psx.put(&pfile.curveBlock, @as(*anyopaque, @ptrCast(block)));
    const samples = ctx.allocBytes(256, 16);
    for (samples[0..256]) |*byte| byte.* = ctx.pick(u8, &.{ 0, 1, 0x7F, 0xFF });
    for (&pfile.curves) |*curve| psx.put(curve, @as(*anyopaque, @ptrCast(samples)));
    const tile_data = ctx.allocBytes(8 * @sizeOf(v.pFileBlock1Data), 16);
    for (tile_data[0 .. 8 * @sizeOf(v.pFileBlock1Data)]) |*byte| byte.* = ctx.int(u8);
    psx.put(&pfile.block1Data, @as(*anyopaque, @ptrCast(tile_data)));
    const tables = ctx.allocBytes(4 * 8 * @sizeOf(v.pFileBlock1Indices), 16);
    for (&pfile.block1Tables, 0..) |*table, t| {
        const entries: [*]v.pFileBlock1Indices = @ptrCast(@alignCast(tables + t * 8 * @sizeOf(v.pFileBlock1Indices)));
        for (entries[0..8]) |*entry| {
            entry.unk0 = ctx.range(u16, 0, 3);
            entry.dataIndex = ctx.range(u16, 0, 5);
        }
        psx.put(table, @as(*anyopaque, @ptrCast(entries)));
    }
    v.g.D_800F569C().* = pfile;
    const parent = ctx.alloc(v.D_800F53B8_t);
    v.g.D_800F53BC().* = if (scenario == 0 and ctx.chance(2)) null else parent;

    const count: usize = if (scenario == 0) 0 else ctx.range(usize, 1, 3);
    var head: ?*v.func_800D6E44_t = null;
    var i: usize = count;
    while (i > 0) {
        i -= 1;
        const node = ctx.alloc(v.func_800D6E44_t);
        for (@as(*[@sizeOf(v.func_800D6E44_t)]u8, @ptrCast(node))) |*byte| byte.* = ctx.int(u8);
        psx.put(&node.next, head);
        head = node;
        // The sprite script: offset, frame or stop, restart, frame, restart (frame
        // times stay below 0x1000, clear of the command nibble; a
        // node started at the last frame reads on past it).
        const script: [*]c_uint = @ptrCast(@alignCast(ctx.allocBytes(20, 16)));
        // The y offset 0 sometimes, so y can sit on the unattached behind test's 0.
        script[0] = 0x10000000 | (ctx.int(c_uint) & (if (ctx.chance(3)) @as(c_uint, 0x3FFF) else 0x0FFFFFFF));
        script[1] = if (ctx.chance(3)) 0x30000000 else 0 | (ctx.range(c_uint, 0, 7)) | (ctx.range(c_uint, 0, 0xFFF) << 16);
        script[2] = 0x20000000 | (ctx.int(c_uint) & 0x0FFFFFFF);
        script[3] = 0 | (ctx.range(c_uint, 0, 7)) | (ctx.range(c_uint, 0, 0xFFF) << 16);
        script[4] = 0x20000000;
        psx.put(&node.sprite.unk18, @as(*anyopaque, @ptrCast(script)));
        node.sprite.unkE = ctx.edge(u16, &.{ 0, 4, 8, 12 });
        node.sprite.unk6 = ctx.edge(u8, &.{ 0, 0, 1, 2, 3, 4, 5 });
        node.sprite.unk7 = ctx.range(u8, 0, 8);
        node.sprite.unk16 = ctx.range(u8, 0, 3);
        const indices = ctx.alloc(v.pFileBlock1Indices);
        indices.unk0 = ctx.range(u16, 0, 3);
        indices.dataIndex = ctx.range(u16, 0, 5);
        psx.put(&node.sprite.unk1C, indices);
        node.x = ctx.range(i32, -0x40000, 0x40000);
        node.y = if (ctx.chance(3)) ctx.range(i32, 0, 0xFFF) else ctx.range(i32, -0x40000, 0x40000);
        node.sprite.unkA = if (ctx.chance(2)) 0 else node.sprite.unkA;
        node.z = ctx.range(i32, -0x40000, 0x40000);
        node.dx = ctx.range(i32, -0x2000, 0x2000);
        node.dy = ctx.range(i32, -0x2000, 0x2000);
        node.dz = ctx.range(i32, -0x2000, 0x2000);
        node.matrixIndex = ctx.range(u8, 0, 15);
        var flags: i32 = 0;
        switch (scenario) {
            2 => flags = 0x20000,
            3 => flags = 0x20000 | 0x80000,
            4 => flags = 0x80000,
            5 => flags = 0x10000 | (ctx.edge(i32, &.{ 0, 0x20000, 0x80000, 0xA0000 })),
            else => flags = ctx.edge(i32, &.{ 0, 0x10000, 0x20000, 0x80000, 0xA0000, 0x30000, 0xB0000 }),
        }
        node.flags = flags | (ctx.int(i32) & 0x7FF000 & ~@as(i32, 0xB0000)) | (ctx.int(i32) & 0x1FF);
        node.unk60.value = (ctx.int(u32) & 0xFFFFF0FF) | (if (scenario == 7 or ctx.chance(2)) @as(u32, ctx.range(u32, 0, 8)) << 8 else 0);
        const curves = &node.curves.fields;
        curves.age = ctx.int(u8);
        if (scenario == 6 or ctx.chance(2)) {
            curves.rCurve = ctx.range(u8, 0, 32);
            curves.gCurve = ctx.range(u8, 0, 32);
            curves.bCurve = ctx.range(u8, 0, 32);
        } else {
            curves.rCurve = 0;
            curves.gCurve = 0;
            curves.bCurve = 0;
        }
    }

    const packets = ctx.allocBytes(0x1000, 16);
    const ot = ctx.allocBytes(0x800 * 4 + 128, 16);
    for (ot[0 .. 0x800 * 4 + 128]) |*byte| byte.* = ctx.int(u8);
    // vs_scratch is a global here, not the PS1 scratchpad address.
    psx.put(&v.g.vs_scratch().unk0, @as(?*anyopaque, @ptrCast(packets)));
    psx.put(&v.g.vs_scratch().unk4, @as(?*anyopaque, @ptrCast(ot + 64)));
    v.g.vs_main_nearClip().* = ctx.edge(i32, &.{ -20, 0, 5, 100, 0x7FF });
    v.g.vs_main_projectionDistance().* = ctx.pick(i32, &.{ 0, 0x1000, 0x100, -0x100 });
    const arctan = v.g.D_80040A14();
    var k: usize = 0;
    while (k < 600) : (k += 1) arctan[k] = ctx.int(u8);
    return .{head};
}

pub fn classify(args: Args) usize {
    _ = args;
    return scenario;
}

pub const stubs = struct {
    pub fn MFC2(ctx: *zd.Ctx, reg: c_int) c_uint {
        const r = ctx.value(u32);
        return switch (reg) {
            // Depth 0 sometimes (the division guard), and the far-clip edge 0x7FF.
            19 => if (r % 8 == 0) r & 3 else if (r % 8 == 1) 0x1FFC | (r >> 8 & 3) else r & 0x1FFF,
            // The behind test's depth at 0 and either side.
            26 => if (r % 4 == 0) ([_]c_uint{ 0, 1, 0xFFFFFFFF })[(r >> 8) % 3] else r,
            // Screen points from a small set half the time: the tilt's two
            // projections equal (no delta) or |dx| == |dy|.
            14 => if (r % 2 == 0) ([_]c_uint{ 0, 0x00010001, 0x00020002, 0xFFFF0001, 0x0001FFFF })[(r >> 8) % 5] else r,
            else => r,
        };
    }
    pub fn pc_particles_native_end(ctx: *zd.Ctx, packet: anytype, ot: anytype) [*c]c_uint {
        _ = .{ ctx, ot };
        return if (end_null) null else packet + 1;
    }
};
