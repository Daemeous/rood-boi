//! Pass-through battle action module; the original remains the implementation.
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
    common.route.exportAll(@backingInt(ownership.Subsystem.battle_action), ownership.namesOf(.battle_action), BattleAction);
}
