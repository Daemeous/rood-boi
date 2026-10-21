//! The battle overlay's owned code: the pass-through battle action module (the
//! original remains the implementation) and the ported functions.
const common = @import("common");
const ownership = @import("ownership");
pub const panic = common.Panic;
const orig = common.route.original;

const BattleAction = struct {
    pub fn func_80070F28(arg0: c_int) callconv(.c) void {
        orig("func_80070F28", @TypeOf(func_80070F28))(arg0);
    }
    pub fn func_8007138C() callconv(.c) void {
        orig("func_8007138C", @TypeOf(func_8007138C))();
    }
    pub fn _handleCombatCameraZoom() callconv(.c) void {
        orig("_handleCombatCameraZoom", @TypeOf(_handleCombatCameraZoom))();
    }
    pub fn func_80078748() callconv(.c) void {
        orig("func_80078748", @TypeOf(func_80078748))();
    }
    pub fn func_800704B0() callconv(.c) void {
        orig("func_800704B0", @TypeOf(func_800704B0))();
    }
    pub fn func_800787F0() callconv(.c) void {
        orig("func_800787F0", @TypeOf(func_800787F0))();
    }
    pub fn func_80070278() callconv(.c) void {
        orig("func_80070278", @TypeOf(func_80070278))();
    }
    pub fn func_80079030() callconv(.c) void {
        orig("func_80079030", @TypeOf(func_80079030))();
    }
    pub fn func_80078AB4() callconv(.c) void {
        orig("func_80078AB4", @TypeOf(func_80078AB4))();
    }
};

comptime {
    common.route.exportAll(@backingInt(ownership.Subsystem.battle_action), ownership.routesOf(.battle_action), BattleAction);
}

// Ported battle functions (PORTING.md): modules named by meaning, routed by
// their original names through ownership.toml's `zig` paths from here.
pub const score = @import("score.zig");
pub const actor_transform = @import("actor_transform.zig");
pub const scene_message = @import("scene_message.zig");
// The model bake-off (pc/docs/porting/battle/BAKEOFF.md): one module per function.
pub const room_timer = @import("room_timer.zig");
pub const actor_weapon = @import("actor_weapon.zig");
pub const field_menu = @import("field_menu.zig");
pub const hit_factors = @import("hit_factors.zig");
pub const room_lock = @import("room_lock.zig");
pub const actor_defeat = @import("actor_defeat.zig");
pub const ability_window = @import("ability_window.zig");
pub const weapon_stats = @import("weapon_stats.zig");
pub const vector_lerp = @import("vector_lerp.zig");
pub const animation_lookup = @import("animation_lookup.zig");
pub const actor_animation = @import("actor_animation.zig");

// The port pipeline (pc/tools/port_pipeline.py): one module per function.
pub const func_80069D14 = @import("func_80069D14.zig");
pub const insert_actor = @import("insert_actor.zig");
pub const turn_toward = @import("turn_toward.zig");
pub const func_800A2574 = @import("func_800A2574.zig");
pub const func_800A25EC = @import("func_800A25EC.zig");
pub const func_800A2790 = @import("func_800A2790.zig");
pub const func_800A291C = @import("func_800A291C.zig");
pub const project_bone_point = @import("project_bone_point.zig");
pub const find_bone = @import("find_bone.zig");
pub const func_800A1648 = @import("func_800A1648.zig");
pub const func_800A1720 = @import("func_800A1720.zig");
pub const func_800A17BC = @import("func_800A17BC.zig");
pub const func_800A190C = @import("func_800A190C.zig");
pub const func_800A1AF8 = @import("func_800A1AF8.zig");
pub const func_800A1B28 = @import("func_800A1B28.zig");
pub const func_800A1B9C = @import("func_800A1B9C.zig");
pub const bone_view_point = @import("bone_view_point.zig");
pub const bone_view_matrix = @import("bone_view_matrix.zig");
pub const start_jump = @import("start_jump.zig");
pub const func_800E5568 = @import("func_800E5568.zig");
pub const action_radius = @import("action_radius.zig");
pub const func_800E5998 = @import("func_800E5998.zig");
pub const func_800E5A74 = @import("func_800E5A74.zig");
pub const flood_region = @import("flood_region.zig");
pub const func_800E6158 = @import("func_800E6158.zig");
pub const func_800E5FDC = @import("func_800E5FDC.zig");
pub const label_regions = @import("label_regions.zig");
pub const action_range_squared = @import("action_range_squared.zig");
pub const fill_action_metadata = @import("fill_action_metadata.zig");
pub const init_actor_state = @import("init_actor_state.zig");
pub const apply_actor_settings = @import("apply_actor_settings.zig");

comptime {
    common.route.exportAll(@backingInt(ownership.Subsystem.battle_146C), ownership.routesOf(.battle_146C), @This());
    common.route.exportAll(@backingInt(ownership.Subsystem.battle_44F14), ownership.routesOf(.battle_44F14), @This());
    common.route.exportAll(@backingInt(ownership.Subsystem.battle_5BF94), ownership.routesOf(.battle_5BF94), @This());
    common.route.exportAll(@backingInt(ownership.Subsystem.battle_146C_bakeoff), ownership.routesOf(.battle_146C_bakeoff), @This());
    common.route.exportAll(@backingInt(ownership.Subsystem.battle_5BF94_bakeoff), ownership.routesOf(.battle_5BF94_bakeoff), @This());
    common.route.exportAll(@backingInt(ownership.Subsystem.battle_30DB0), ownership.routesOf(.battle_30DB0), @This());
    common.route.exportAll(@backingInt(ownership.Subsystem.battle_40564), ownership.routesOf(.battle_40564), @This());
    common.route.exportAll(@backingInt(ownership.Subsystem.battle_146C_port), ownership.routesOf(.battle_146C_port), @This());
    common.route.exportAll(@backingInt(ownership.Subsystem.battle_38C1C_port), ownership.routesOf(.battle_38C1C_port), @This());
    common.route.exportAll(@backingInt(ownership.Subsystem.battle_7CD68_port), ownership.routesOf(.battle_7CD68_port), @This());
}
