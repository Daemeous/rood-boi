//! zig_diff spec: vs_battle_exec -> battle_exec.battleExec (146C.c).
//!
//! vs_battle_exec never returns: it is a `while (1)` over the frame loop. A
//! run therefore ends in a stub: `stopAfter` (below) names the stub call
//! after which the run stops, `limit` stub calls into it. That needs the
//! harness's stop hook (main.zig `stopAfter`: each side
//! runs on a thread a stopping stub exits); without it the run never ends.
//! A run of `limit` calls is a part of one frame or several frames; the frame
//! loops, the pad break, the transition exits and the zone reload all run in
//! the same way as the C, and every call carries the state hash.
//!
//! Inputs: one scenario per class (the input classes below), the globals the
//! function reads set for it with edge values around every test of the C
//! (buttons, the character's status, the camera mode, the transition step and
//! alpha). Every callee is a recording stub; those whose result steers the
//! C return a flag, a small selector or an arena object, so both sides of
//! every test run. The character, the 16 actor slots, the room data and the
//! ordering tables are arena objects.
//!
//! Not reachable (the C reads an uninitialised local there): a screen
//! transition (step 1, 2 or 4) with mode 0, 1 or 2 reads `skipCameraUpdate`
//! unassigned; the transition scenarios use modes 3 and up, where the read
//! changes nothing.
const std = @import("std");
const zd = @import("zdiff");
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").battleExec));

const Scenario = enum(usize) {
    pad_break,
    transition_mode3,
    transition_other,
    zone_reload,
    mode0,
    field,
    mode2_cameras,
    charge_skip_camera4,
    charge_skip_camera5,
    mode3_first_person,
    mode3_camera,
    mode4,
    mode5,
    mode6,
    mode7_first_person_exit,
    mode7_cameras,
    mode8,
    mode9,
    mode10,
    mode11,
    mode12,
    mode_unknown,
    room_checkpoint,
};

pub const classes = [_][:0]const u8{
    "pad_break",
    "transition_mode3",
    "transition_other",
    "zone_reload",
    "mode0",
    "field",
    "mode2_cameras",
    "charge_skip_camera4",
    "charge_skip_camera5",
    "mode3_first_person",
    "mode3_camera",
    "mode4",
    "mode5",
    "mode6",
    "mode7_first_person_exit",
    "mode7_cameras",
    "mode8",
    "mode9",
    "mode10",
    "mode11",
    "mode12",
    "mode_unknown",
    "room_checkpoint",
};

// What the stubs and the classes read, chosen by prepare.
var scenario: Scenario = .pad_break;
var limit: u32 = 0;
var pad_break: bool = false;
var skip_charge: bool = false;
/// Steering toward branches random inputs rarely reach: `draw` the field's
/// weapon-draw path (its sound and the camera skip), `unlock` the field's
/// lock-on test with status 0x4000 alone, `ready15` the first-person ready
/// loop with only actor 15 busy, `ready_free` the same loop with no actor busy,
/// `open` the field's pad tests with the early exits (warp, door, hook) off:
/// `open_variant` 0 the toggle from a held lock-on (previous pad bit 0), 1 the
/// pending lock-on (D_800F1858), 2 the press of 0x10, 3 the press of 0x40.
const Steer = enum { none, draw, unlock, open, ready15, ready_free };
var steer: Steer = .none;
var open_variant: u8 = 0;
var present: [16]bool = undefined;
var room_a: *v._mpdRoomSectionA = undefined;
var room_13: *v._mpdRoomSection13 = undefined;
var menu_object: *v.D_800F18EC_t = undefined;

pub fn stopAfter(call: []const u8, calls: u32) bool {
    _ = call;
    return calls >= limit;
}

fn buttons(ctx: *zd.Ctx) u32 {
    return ctx.pick(u32, &.{ 0, 0x10, 0x20, 0x40, 0x80, 0x400, 0x800, 0xC00, 0x1000, 0x2000, 0x4000, 0x8000, 0x9FF, 0x600 });
}

pub fn prepare(ctx: *zd.Ctx) Args {
    scenario = @enumFromInt(ctx.range(usize, 0, classes.len - 1));
    steer = .none;
    open_variant = 0;
    limit = if (ctx.chance(5)) ctx.range(u32, 150, 900) else ctx.range(u32, 1, 150);
    pad_break = scenario == .pad_break;
    skip_charge = ((scenario == .charge_skip_camera4 or scenario == .charge_skip_camera5) and ctx.chance(2)) or ctx.chance(3);

    v.g.D_800F1858().* = ctx.pick(i32, &.{ 0, 1 });
    v.g.vs_main_startState().* = ctx.pick(i32, &.{ 3, 4, 0, 1, 2, 5 });
    v.g.vs_main_frameBuf().* = ctx.edge(i32, &.{ 0, 1 });
    v.g.vs_main_buttonsPressed().all = buttons(ctx);
    v.g.vs_main_buttonsPreviousState().* = ctx.pick(i32, &.{ 0, 1, 2, 3, -1 });
    v.g.D_800F18FC().* = ctx.int(i32);
    v.g.D_800F1868().* = ctx.pick(i32, &.{ 0, 1, 2, 3, 4, -1 });
    v.g.D_800F1B98().* = ctx.pick(i32, &.{ 0, 1, 2, 3 });
    v.g.vs_gametime_tickspeed().* = ctx.pick(i32, &.{ -3, -1, 0, 1, 2, 3, 4, 8, 9 });
    v.g.vs_battle_menuState().currentMenu = ctx.int(u8);

    // The ordering tables and the packet cursors.
    for (0..2) |k| {
        v.g.D_80055C80()[k] = @ptrCast(ctx.allocBytes(0x40, 16));
        v.g.D_8005E0C0()[k] = @ptrCast(ctx.allocBytes(0x40, 16));
    }

    // The character, its stats and the actor slots.
    const stats = ctx.alloc(v.vs_battle_actor2);
    stats.unk954 = ctx.pick(u16, &.{ 0, 1 });
    stats.statuses = @bitCast(ctx.pick(u32, &.{ 0, 0x2000, 0x4000, 0x6000 }));
    const character = ctx.alloc(v.vs_battle_actor);
    psx.put(&character.stats, stats);
    character.weaponDrawn = ctx.pick(i32, &.{ 0, 1, 2, 3 });
    v.g.vs_battle_characterState().* = character;
    for (0..16) |k| {
        present[k] = !ctx.chance(3);
        if (present[k]) {
            const actor = ctx.alloc(v.vs_battle_actor);
            actor.unk24 = ctx.pick(u8, &.{ 4, 0, 1 });
            v.g.vs_battle_actors()[k] = actor;
        } else {
            v.g.vs_battle_actors()[k] = null;
        }
    }
    v.g.D_800F19CC().* = ctx.alloc(v.D_800F19CC_t);
    room_a = ctx.alloc(v._mpdRoomSectionA);
    room_13 = ctx.alloc(v._mpdRoomSection13);
    menu_object = ctx.alloc(v.D_800F18EC_t);

    // The camera: the first-person facing, and the delta of a transition.
    v.g.vs_battle_cameraCurrentSpherical().delta.yaw = ctx.pick(i32, &.{ 0, 1, -1 });
    v.g.vs_battle_cameraCurrentSpherical().delta.mode = ctx.pick(i32, &.{ 0, 1, 2, 3, 4 });

    // The room data the diagnostic reads.
    v.g.vs_battle_roomData().unk1 = ctx.pick(u8, &.{ 0, 1 });
    if (ctx.chance(2)) {
        psx.put(&v.g.vs_battle_roomData().geometrySection, ctx.allocBytes(0x40, 16));
    } else {
        psx.put(&v.g.vs_battle_roomData().geometrySection, null);
    }
    v.g.D_800F190C().* = ctx.pick(i32, &.{ 0, 1, 2 });
    common.psx.global(c_int, "pc_room_checkpoint_requested").* = if (ctx.chance(8)) 1 else 0;

    // The mode, the camera mode and the screen transition of the scenario.
    var mode: u32 = ctx.range(u32, 0, 12);
    var camera: u32 = ctx.pick(u32, &.{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 });
    var step: u32 = ctx.edge(u32, &.{ 0, 3, 5, 6, 0xFFFF_FFFF });
    var alpha: i32 = ctx.pick(i32, &.{ 255, 0, 254, -1, 256 });
    switch (scenario) {
        .pad_break => {
            mode = ctx.range(u32, 0, 13);
            step = ctx.pick(u32, &.{ 1, 2, 4, 0, 3 });
        },
        .transition_mode3 => {
            mode = 3;
            step = ctx.edge(u32, &.{ 1, 2, 4 });
            alpha = ctx.edge(i32, &.{ 0, 254, 256, -1, 128 });
        },
        .transition_other => {
            mode = ctx.range(u32, 4, 13);
            step = ctx.edge(u32, &.{ 1, 2, 4 });
            alpha = ctx.edge(i32, &.{ 0, 254, 256, -1, 128, 255 });
        },
        .zone_reload => {
            mode = ctx.range(u32, 3, 13);
            step = ctx.edge(u32, &.{ 1, 2 });
            alpha = 255;
        },
        .mode0 => mode = 0,
        .field => {
            mode = 1;
            steer = @enumFromInt(ctx.range(u8, 0, 3));
            if (steer == .open) {
                open_variant = ctx.range(u8, 0, 3);
                limit = ctx.range(u32, 40, 300);
                stats.unk954 = ctx.pick(u16, &.{ 0, 0, 0, 1 });
                switch (open_variant) {
                    0 => {
                        v.g.D_800F1858().* = 0;
                        v.g.vs_main_buttonsPreviousState().* = 1;
                        v.g.vs_main_buttonsPressed().all = ctx.pick(u32, &.{ 0, 0x10 });
                    },
                    1 => {
                        v.g.D_800F1858().* = 1;
                        v.g.vs_main_buttonsPreviousState().* = ctx.pick(i32, &.{ 0, 2 });
                        v.g.vs_main_buttonsPressed().all = ctx.pick(u32, &.{ 0, 0x10 });
                    },
                    2 => {
                        v.g.D_800F1858().* = 0;
                        v.g.vs_main_buttonsPreviousState().* = ctx.pick(i32, &.{ 0, 2 });
                        v.g.vs_main_buttonsPressed().all = 0x10;
                    },
                    else => {
                        v.g.D_800F1858().* = 0;
                        v.g.vs_main_buttonsPreviousState().* = ctx.pick(i32, &.{ 0, 2 });
                        v.g.vs_main_buttonsPressed().all = 0x40;
                    },
                }
            } else if (steer != .none) {
                limit = ctx.range(u32, 60, 300);
                v.g.D_800F1858().* = 0;
                v.g.vs_main_buttonsPreviousState().* = 0;
                v.g.vs_main_buttonsPressed().all = if (steer == .draw) 0x20 else 0x40;
                if (steer == .draw) {
                    stats.unk954 = ctx.pick(u16, &.{ 0, 0, 1 });
                    stats.statuses = @bitCast(ctx.pick(u32, &.{ 0, 0x2000 }));
                    character.weaponDrawn = ctx.pick(i32, &.{ 1, 1, 0 });
                }
            }
        },
        .mode2_cameras => {
            mode = 2;
            camera = ctx.pick(u32, &.{ 0, 1, 2, 3, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 });
        },
        .charge_skip_camera4 => {
            mode = 2;
            camera = 4;
        },
        .charge_skip_camera5 => {
            mode = 2;
            camera = 5;
        },
        .mode3_first_person => {
            mode = 3;
            camera = 1;
            if (ctx.chance(2)) {
                steer = if (ctx.chance(2)) .ready15 else .ready_free;
                v.g.vs_battle_cameraCurrentSpherical().delta.yaw = 0;
                v.g.vs_battle_cameraCurrentSpherical().delta.mode = ctx.pick(i32, &.{ 1, 3 });
                if (steer == .ready15 and !present[15]) {
                    present[15] = true;
                    v.g.vs_battle_actors()[15] = ctx.alloc(v.vs_battle_actor);
                }
            }
        },
        .mode3_camera => {
            mode = 3;
            camera = ctx.edge(u32, &.{ 0, 2, 3, 4 });
        },
        .mode4 => mode = 4,
        .mode5 => {
            mode = 5;
            camera = ctx.edge(u32, &.{ 0, 1 });
        },
        .mode6 => mode = 6,
        .mode7_first_person_exit => {
            mode = 7;
            camera = 1;
        },
        .mode7_cameras => {
            mode = 7;
            camera = ctx.edge(u32, &.{ 0, 2, 3 });
        },
        .mode8 => {
            mode = 8;
            camera = ctx.edge(u32, &.{ 0, 1, 2, 3, 4 });
        },
        .mode9 => {
            mode = 9;
            camera = ctx.edge(u32, &.{ 0, 1, 2, 3, 4 });
        },
        .mode10 => mode = 10,
        .mode11 => mode = 11,
        .mode12 => mode = 12,
        .mode_unknown => mode = ctx.edge(u32, &.{ 13, 14, 0xFFFF_FFFF }),
        .room_checkpoint => {
            v.g.D_800F190C().* = 1;
            common.psx.global(c_int, "pc_room_checkpoint_requested").* = 1;
        },
    }
    v.g.D_800F196C().* = mode;
    v.g._cameraMode().* = camera;
    v.g.vs_battle_screenTransitionStep().* = step;
    v.g.vs_battle_screenTransitionAlpha().* = alpha;
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    return @intFromEnum(scenario);
}

fn flag(ctx: *zd.Ctx) c_int {
    return ctx.value(u1);
}

/// 1 one time in `n`, else 0.
fn rarely(ctx: *zd.Ctx, n: u8) c_int {
    return @intFromBool(ctx.value(u8) % n == 0);
}

pub const stubs = struct {
    pub fn pc_weapon_slots_process_pad(ctx: *zd.Ctx) c_int {
        if (pad_break) return -1;
        return if (ctx.value(u1) == 1) 0 else ctx.value(i8);
    }

    /// The status word: the bits the C tests, set at random, or none, or all.
    pub fn func_800A0BE0(ctx: *zd.Ctx, actor: anytype) c_uint {
        switch (steer) {
            .draw => return ctx.pick(u32, &.{ 0, 0, 0x180000 }),
            .unlock => return ctx.pick(u32, &.{ 0x4000, 0x20004000 }),
            .ready15 => return if (actor == 15) 0x4000 else 0,
            .open => return if (open_variant == 3) 0 else ctx.pick(u32, &.{ 0, 0, 0, 0x4000 }),
            .ready_free => return 0,
            .none => {},
        }
        const mask: u32 = 0x2000 | 0x4000 | 0x180000 | 0x3F4000 | 0x20004000;
        return switch (ctx.value(u8) % 4) {
            0 => 0,
            1 => mask,
            else => ctx.value(u32) & mask,
        };
    }

    pub fn func_800BEC58(ctx: *zd.Ctx, a: anytype, b: anytype, cc: anytype, d: anytype) c_short {
        _ = .{ b, cc, d };
        if (steer == .draw or steer == .unlock) return 0;
        if (steer == .open and a == 0x20) return 0;
        return switch (ctx.value(u8) % 4) {
            0, 1 => 1,
            2 => 0,
            else => ctx.edge(i16, &.{ 2, 0x7FFF, 0x100, -2, -0x8000, ctx.value(i16) }),
        };
    }

    pub fn pc_room_warp_poll(ctx: *zd.Ctx) c_int {
        if (steer != .none) return 0;
        return rarely(ctx, 4);
    }

    pub fn func_80074BAC(ctx: *zd.Ctx, a: anytype, b: anytype) c_int {
        _ = .{ a, b };
        if (steer != .none) return 0;
        return rarely(ctx, 4);
    }

    pub fn func_800CACD0(ctx: *zd.Ctx, a: anytype, b: anytype) c_int {
        _ = .{ a, b };
        return flag(ctx);
    }

    pub fn func_800A38E0(ctx: *zd.Ctx, a: anytype) c_int {
        _ = a;
        if (steer == .unlock) return -1;
        if (steer == .open) return ctx.edge(i8, &.{ -1, -1, 0, 3 });
        return ctx.value(i8);
    }

    /// An actor slot that is filled, or -1.
    pub fn func_800A3760(ctx: *zd.Ctx, a: anytype, b: anytype, cc: anytype) c_int {
        _ = .{ a, b, cc };
        const k: c_int = if (ctx.value(u8) % 3 == 0) 0 else ctx.value(u8) % 17;
        if (k == 16 or !present[@intCast(k)]) return -1;
        return k;
    }

    pub fn func_80074860(ctx: *zd.Ctx, a: anytype) c_int {
        _ = a;
        return rarely(ctx, 3);
    }

    pub fn func_800748B8(ctx: *zd.Ctx, a: anytype) [*c]c._mpdRoomSectionA {
        _ = a;
        return if (ctx.value(u1) == 1) @ptrCast(room_a) else null;
    }

    pub fn func_80073AFC(ctx: *zd.Ctx, a: anytype) c_int {
        _ = a;
        return flag(ctx);
    }

    pub fn func_80074950(ctx: *zd.Ctx, a: anytype) ?*c._mpdRoomSection13 {
        _ = a;
        return if (ctx.value(u1) == 1) @ptrCast(room_13) else null;
    }

    pub fn func_8006E7F0(ctx: *zd.Ctx) c_int {
        return rarely(ctx, 4);
    }

    pub fn func_80075554(ctx: *zd.Ctx) c_int {
        return ctx.value(u8) % 3;
    }

    pub fn func_800CEEBC(ctx: *zd.Ctx) c_int {
        return flag(ctx);
    }

    pub fn func_800CB66C(ctx: *zd.Ctx) ?*c.D_800F18EC_t {
        menu_object.unk0 = if (ctx.value(u1) == 1) 2 else ctx.value(i16);
        return @ptrCast(menu_object);
    }

    pub fn func_800CB45C(ctx: *zd.Ctx) c_int {
        return flag(ctx);
    }

    pub fn func_80088B6C(ctx: *zd.Ctx) c_int {
        return ctx.value(u8) % 3;
    }

    pub fn func_8007418C(ctx: *zd.Ctx) c_int {
        return flag(ctx);
    }

    pub fn func_80074294(ctx: *zd.Ctx) c_int {
        return flag(ctx);
    }

    pub fn func_80074120(ctx: *zd.Ctx) c_int {
        return flag(ctx);
    }

    pub fn func_8007424C(ctx: *zd.Ctx) c_int {
        return flag(ctx);
    }

    pub fn func_800744B8(ctx: *zd.Ctx) c_int {
        return flag(ctx);
    }

    pub fn func_80074374(ctx: *zd.Ctx) c_int {
        return flag(ctx);
    }

    pub fn _isLookAtAtDestination(ctx: *zd.Ctx) c_int {
        return flag(ctx);
    }

    pub fn func_8009E4B0(ctx: *zd.Ctx, a: anytype) c_uint {
        _ = a;
        return @intCast(flag(ctx));
    }

    pub fn func_800A47C4(ctx: *zd.Ctx) c_int {
        return flag(ctx);
    }

    pub fn vs_battle_validateShortcutSelection(ctx: *zd.Ctx, shortcut: anytype) c_int {
        _ = shortcut;
        return flag(ctx);
    }

    pub fn pc_charge_skip(ctx: *zd.Ctx, phase: anytype) c_int {
        _ = phase;
        return if (skip_charge) 1 else flag(ctx);
    }

    pub fn pc_camera_stick_menu(ctx: *zd.Ctx, a: anytype, b: anytype, cc: anytype, d: anytype) c_int {
        _ = .{ a, b, cc, d };
        return flag(ctx);
    }
};
