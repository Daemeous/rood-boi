//! zig_diff spec: func_8008AC78 -> scene_submit.func_8008AC78 (146C.c, scene_submit.inc).
//! Inputs: no arguments. The tick loop's count (tickspeed -3..5) and its test (D_800F1BA4);
//! the room state's first word at 0x101 or beside it (func_8008B1FC may change it: the
//! `flip` stub); the geometry and map sections present or not; the camera angle across the
//! three compass shifts; simpleMap bits 0 and 1; the map's fade D_800F1CD4 at the clamps
//! and its direction D_800F1CD6; the draw environment's clip x either side of 320 and the
//! frame buffer index; the character's weapon drawn flag; the pulse counter. The packet
//! cursor and the OT are arena blocks. func_8008B590 and vs_battle_getGeomOffset fill their
//! out-parameters from the stub values, so the locals are deterministic.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8008AC78));

pub const classes = [_][:0]const u8{ "not-in-room", "state-changed", "no-map", "map-faded-out", "map-no-frame", "frame-weapon", "frame-idle" };

var flip: bool = false;

pub const stubs = struct {
    pub fn func_8008B1FC(ctx: *zd.Ctx, section: ?*anyopaque) void {
        _ = ctx;
        _ = section;
        if (flip) roomWord().* = 0x102;
    }
    pub fn func_8008B590(ctx: *zd.Ctx, m: [*c]v.MATRIX, angle: c_int) void {
        _ = angle;
        m.* = std.mem.zeroes(v.MATRIX);
        m.*.m[0][0] = ctx.value(i16);
        m.*.t[1] = ctx.value(i32);
    }
    /// The draw-area rectangle is a stack local of the function: copy it where the harness compares it.
    pub fn SetDrawArea(ctx: *zd.Ctx, area: anytype, rect: anytype) void {
        _ = area;
        const seen = ctx.alloc(v.RECT);
        seen.* = @as(*const v.RECT, @ptrCast(@alignCast(rect))).*;
    }
    /// The geometry offset set (the map's, then the saved one back) is a stack local too.
    pub fn vs_battle_setGeomOffset(ctx: *zd.Ctx, offset: anytype) void {
        const seen = ctx.alloc(v.vs_battle_geomOffset);
        seen.* = @as(*const v.vs_battle_geomOffset, @ptrCast(@alignCast(offset))).*;
    }
    pub fn vs_battle_getGeomOffset(ctx: *zd.Ctx, offset: [*c]v.vs_battle_geomOffset) void {
        offset.*.x = ctx.value(i32);
        offset.*.y = ctx.value(i32);
    }
};

fn roomWord() *u16 {
    return @ptrCast(v.g.vs_battle_roomData());
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const scratch: *v.vs_scratch_t = @ptrFromInt(0x1F800000);
    scratch.unk0.raw = psx.addr(ctx.allocBytes(512, 16));
    scratch.unk4.raw = psx.addr(ctx.allocBytes(64, 16));
    // each compass shift's boundary (0x41, 0x101, 0x1C1 mod 0x200: the angle one past a multiple of 0x200)
    scratch.camera.angles.vy = ctx.pick(i32, &.{ 0, 0x40, 0x80, 0x840, 0x900, 0x9C0, 0xFFF, 0x1000, -1, -0x800, 0x41, 0x101, 0x1C1, 0x241, 0x301, 0x3C1, 0xA41, 0xB01, 0xBC1 });
    scratch.firstPersonViewFacing = ctx.int(u32);

    v.g.vs_gametime_tickspeed().* = ctx.edge(i32, &.{ -3, 0, 1, 2, 3, 5, 4 });
    v.g.D_800F1BA4().* = ctx.edge(u16, &.{ 0, 0, 0, 1, 0x100 });

    roomWord().* = ctx.edge(u16, &.{ 0x101, 0x101, 0x101, 0x101, 0x100, 0x001, 0x201, 0x102 });
    const room = v.g.vs_battle_roomData();
    psx.put(&room.geometrySection, if (ctx.chance(3)) null else @as(*i32, @ptrCast(@alignCast(ctx.allocBytes(16, 16)))));
    psx.put(&room.sectionF, if (ctx.chance(5)) null else ctx.alloc(v.vs_battle_scene));
    flip = ctx.chance(4);
    v.g.vs_main_settings().simpleMap = ctx.edge(u8, &.{ 0, 1, 2, 3, 3, 3, 0xFF, 0xFC });

    v.g.D_800F1CD6().* = ctx.edge(u8, &.{ 0, 1, 1, 0xFF });
    v.g.D_800F1CD4().* = ctx.edge(i16, &.{ -5, -1, 0, 1, 2, 3, 7, 7, 8, 8, 9, 100 });
    v.g.vs_main_frameBuf().* = ctx.edge(i32, &.{ 0, 1, 2, -1, 0x7FFFFFFF });
    v.g.vs_main_drawEnv()[0].clip.x = ctx.edge(i16, &.{ 0, 319, 320, 321, 640 });
    v.g.vs_main_drawEnv()[1].clip.x = ctx.edge(i16, &.{ 0, 319, 320, 321, 640 });

    const character = ctx.alloc(v.vs_battle_actor);
    character.weaponDrawn = ctx.edge(i32, &.{ 0, 1, 1, 2 });
    v.g.vs_battle_characterState().* = character;
    v.g.D_800F1BA0().* = ctx.pick(i32, &.{ 0, 1, 255, 256, 0x10000, -1 });
    v.g.pc_gte_anchored().* = ctx.int(i32);
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    const room = v.g.vs_battle_roomData();
    if (roomWord().* != 0x101) return 0;
    const geometry = psx.get(room.geometrySection) != null;
    if (flip and geometry) return 1;
    if ((v.g.vs_main_settings().simpleMap & 1) == 0 or psx.get(room.sectionF) == null) return 2;
    var fade = v.g.D_800F1CD4().*;
    if (v.g.D_800F1CD6().* != 0) {
        fade +%= 1;
        if (fade > 8) fade = 8;
    } else {
        fade -%= 2;
        if (fade < 0) fade = 0;
    }
    if (fade <= 0) return 3;
    if ((v.g.vs_main_settings().simpleMap & 2) == 0) return 4;
    const character = v.g.vs_battle_characterState().*.?;
    return if (character.weaponDrawn == 1) 5 else 6;
}
