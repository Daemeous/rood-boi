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
pub const func_800DA5CC = @import("func_800DA5CC.zig");
pub const func_800DA6A4 = @import("func_800DA6A4.zig");
pub const func_800DA1D4 = @import("func_800DA1D4.zig");
pub const func_800DAA6C = @import("func_800DAA6C.zig");
pub const func_800DAC80 = @import("func_800DAC80.zig");
pub const func_800DAAD8 = @import("func_800DAAD8.zig");
pub const func_800DAD9C = @import("func_800DAD9C.zig");
pub const detour_when_stuck = @import("detour_when_stuck.zig");
pub const func_800DA4BC = @import("func_800DA4BC.zig");
pub const func_800DA738 = @import("func_800DA738.zig");
pub const func_800DAED0 = @import("func_800DAED0.zig");
pub const func_800D9E18 = @import("func_800D9E18.zig");
pub const func_800BFE34 = @import("func_800BFE34.zig");
pub const get_short = @import("get_short.zig");
pub const func_800BFD9C = @import("func_800BFD9C.zig");
pub const func_800BFE50 = @import("func_800BFE50.zig");
pub const func_800C00E8 = @import("func_800C00E8.zig");
pub const func_800C0150 = @import("func_800C0150.zig");
pub const func_800BFEBC = @import("func_800BFEBC.zig");
pub const copy_aligned = @import("copy_aligned.zig");
pub const play_sfx10 = @import("play_sfx10.zig");
pub const func_800C02A8 = @import("func_800C02A8.zig");
pub const memcpy = @import("memcpy.zig");
pub const play_invalid_sfx = @import("play_invalid_sfx.zig");
pub const play_menu_select_sfx = @import("play_menu_select_sfx.zig");
pub const play_menu_leave_sfx = @import("play_menu_leave_sfx.zig");
pub const play_menu_change_sfx = @import("play_menu_change_sfx.zig");
pub const func_800C05B4 = @import("func_800C05B4.zig");
pub const func_800C06E0 = @import("func_800C06E0.zig");
pub const func_800C0700 = @import("func_800C0700.zig");
pub const func_800C031C = @import("func_800C031C.zig");
pub const func_800C0738 = @import("func_800C0738.zig");
pub const func_800C0758 = @import("func_800C0758.zig");
pub const func_800C085C = @import("func_800C085C.zig");
pub const func_800C0990 = @import("func_800C0990.zig");
pub const func_800C05EC = @import("func_800C05EC.zig");
pub const func_800D9538 = @import("func_800D9538.zig");
pub const func_800D85D8 = @import("func_800D85D8.zig");
pub const func_800D8400 = @import("func_800D8400.zig");
pub const func_800D8EF4 = @import("func_800D8EF4.zig");
pub const func_800D87E8 = @import("func_800D87E8.zig");
pub const func_800C1034 = @import("func_800C1034.zig");
pub const func_800C110C = @import("func_800C110C.zig");
pub const func_800C1564 = @import("func_800C1564.zig");
pub const func_800C1664 = @import("func_800C1664.zig");
pub const func_800C1D84 = @import("func_800C1D84.zig");
pub const func_800C1A40 = @import("func_800C1A40.zig");
pub const func_800C16FC = @import("func_800C16FC.zig");
pub const func_800C282C = @import("func_800C282C.zig");
pub const func_800C253C = @import("func_800C253C.zig");
pub const func_800C2254 = @import("func_800C2254.zig");
pub const func_800C2368 = @import("func_800C2368.zig");
pub const func_800C0FA8 = @import("func_800C0FA8.zig");
pub const func_800C1384 = @import("func_800C1384.zig");
pub const func_800C20B4 = @import("func_800C20B4.zig");
pub const func_800C123C = @import("func_800C123C.zig");
pub const func_800C16DC = @import("func_800C16DC.zig");
pub const func_800C0D78 = @import("func_800C0D78.zig");
pub const func_800AE474 = @import("func_800AE474.zig");
pub const func_800AE46C = @import("func_800AE46C.zig");
pub const func_800AE68C = @import("func_800AE68C.zig");
pub const func_800AE7D8 = @import("func_800AE7D8.zig");
pub const func_800AE4FC = @import("func_800AE4FC.zig");
pub const func_800AE6C0 = @import("func_800AE6C0.zig");
pub const func_800AE828 = @import("func_800AE828.zig");
pub const func_800AEAE8 = @import("func_800AEAE8.zig");
pub const func_800AEEC4 = @import("func_800AEEC4.zig");
pub const func_800AF844 = @import("func_800AF844.zig");
pub const func_800AF6E8 = @import("func_800AF6E8.zig");
pub const func_800AF960 = @import("func_800AF960.zig");
pub const func_800AFDE8 = @import("func_800AFDE8.zig");
pub const func_800AFA28 = @import("func_800AFA28.zig");
pub const func_800B101C = @import("func_800B101C.zig");
pub const func_800DCBD8 = @import("func_800DCBD8.zig");
pub const func_800DC888 = @import("func_800DC888.zig");
pub const func_800DD344 = @import("func_800DD344.zig");
pub const func_800DD7CC = @import("func_800DD7CC.zig");
pub const func_800DD604 = @import("func_800DD604.zig");
pub const func_800DD000 = @import("func_800DD000.zig");
pub const func_800DEEA4 = @import("func_800DEEA4.zig");
pub const func_800DEC88 = @import("func_800DEC88.zig");
pub const func_800DD918 = @import("func_800DD918.zig");
pub const func_800DCAA0 = @import("func_800DCAA0.zig");
pub const func_800DEB10 = @import("func_800DEB10.zig");
pub const func_800C2B0C = @import("func_800C2B0C.zig");
pub const func_800AE980 = @import("func_800AE980.zig");
pub const func_800D78CC = @import("func_800D78CC.zig");
pub const func_800D78B8 = @import("func_800D78B8.zig");
pub const func_800D7890 = @import("func_800D7890.zig");
pub const func_800D7814 = @import("func_800D7814.zig");
pub const parse_pfile_block5 = @import("parse_pfile_block5.zig");
pub const parse_pfile_block3 = @import("parse_pfile_block3.zig");
pub const func_800D78F0 = @import("func_800D78F0.zig");
pub const parse_pfile_block9 = @import("parse_pfile_block9.zig");
pub const parse_pfile_block8 = @import("parse_pfile_block8.zig");
pub const parse_pfile_curve_block = @import("parse_pfile_curve_block.zig");
pub const parse_pfile_block10 = @import("parse_pfile_block10.zig");
pub const parse_pfile_block2 = @import("parse_pfile_block2.zig");
pub const parse_pfile_block11 = @import("parse_pfile_block11.zig");
pub const parse_pfile_block12 = @import("parse_pfile_block12.zig");
pub const func_800D7A90 = @import("func_800D7A90.zig");
pub const parse_pfile_block13 = @import("parse_pfile_block13.zig");
pub const parse_pfile_block1 = @import("parse_pfile_block1.zig");
pub const configure_plg = @import("configure_plg.zig");
pub const func_800D7C5C = @import("func_800D7C5C.zig");
pub const set_effect_exec = @import("set_effect_exec.zig");
pub const func_800D7FFC = @import("func_800D7FFC.zig");
pub const configure_effect_fb_load = @import("configure_effect_fb_load.zig");
pub const func_800D8038 = @import("func_800D8038.zig");
pub const func_800D8044 = @import("func_800D8044.zig");
pub const set_plg_load_state = @import("set_plg_load_state.zig");
pub const get_main_menu_lba = @import("get_main_menu_lba.zig");
pub const func_800D8060 = @import("func_800D8060.zig");
pub const func_800D7BF8 = @import("func_800D7BF8.zig");
pub const func_800D7EF4 = @import("func_800D7EF4.zig");
pub const func_800D7CFC = @import("func_800D7CFC.zig");
pub const load_eff_purge = @import("load_eff_purge.zig");
pub const func_800DF9A8 = @import("func_800DF9A8.zig");
pub const func_800DFA54 = @import("func_800DFA54.zig");
pub const func_800DFAF8 = @import("func_800DFAF8.zig");
pub const func_800E0850 = @import("func_800E0850.zig");
pub const func_800E0678 = @import("func_800E0678.zig");
pub const func_800E0918 = @import("func_800E0918.zig");
pub const func_800E02B4 = @import("func_800E02B4.zig");
pub const func_800E1238 = @import("func_800E1238.zig");
pub const func_800E1388 = @import("func_800E1388.zig");
pub const func_800E1850 = @import("func_800E1850.zig");
pub const func_800E1764 = @import("func_800E1764.zig");
pub const func_800E1908 = @import("func_800E1908.zig");
pub const func_800E1BB8 = @import("func_800E1BB8.zig");
pub const func_800E23AC = @import("func_800E23AC.zig");
pub const func_800E2CCC = @import("func_800E2CCC.zig");
pub const func_800E3600 = @import("func_800E3600.zig");
pub const func_800E153C = @import("func_800E153C.zig");
pub const func_800E19FC = @import("func_800E19FC.zig");
pub const func_800E2B2C = @import("func_800E2B2C.zig");

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
    common.route.exportAll(@backingInt(ownership.Subsystem.battle_71618_port), ownership.routesOf(.battle_71618_port), @This());
    common.route.exportAll(@backingInt(ownership.Subsystem.battle_573B8_port), ownership.routesOf(.battle_573B8_port), @This());
    common.route.exportAll(@backingInt(ownership.Subsystem.battle_6FC00_port), ownership.routesOf(.battle_6FC00_port), @This());
    common.route.exportAll(@backingInt(ownership.Subsystem.battle_58578_port), ownership.routesOf(.battle_58578_port), @This());
    common.route.exportAll(@backingInt(ownership.Subsystem.battle_44F14_port), ownership.routesOf(.battle_44F14_port), @This());
    common.route.exportAll(@backingInt(ownership.Subsystem.battle_74088_port), ownership.routesOf(.battle_74088_port), @This());
    common.route.exportAll(@backingInt(ownership.Subsystem.battle_6F014_port), ownership.routesOf(.battle_6F014_port), @This());
    common.route.exportAll(@backingInt(ownership.Subsystem.battle_771A8_port), ownership.routesOf(.battle_771A8_port), @This());
}
