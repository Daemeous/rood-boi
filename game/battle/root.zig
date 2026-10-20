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

comptime {
    common.route.exportAll(@backingInt(ownership.Subsystem.battle_146C), ownership.routesOf(.battle_146C), @This());
    common.route.exportAll(@backingInt(ownership.Subsystem.battle_44F14), ownership.routesOf(.battle_44F14), @This());
    common.route.exportAll(@backingInt(ownership.Subsystem.battle_5BF94), ownership.routesOf(.battle_5BF94), @This());
}
