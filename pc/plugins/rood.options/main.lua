-- rood.options: the pages of the Option screens (pc/docs/Extensions.md "Option pages";
-- plan pc/docs/plans/2026-10-18-lua-m1-options.md). In the game's own Option screen they
-- are entries below its rows; the modern System tab lists them too. A row is a setting's
-- key (the settings catalog, pc/src/ui/ui_settings.cpp; rood.settings.list() names them),
-- or a table: { setting = key, label = ..., help = ..., where = "classic" | "modern" },
-- { header = text }, { help = text }, { page = id } (a link), { action = id }.
--
-- With this plugin off (or broken) the C++ fallback (pc/src/ui/option_pages.cpp) builds
-- the same pages from each setting's home page: keep the two equal (a probe compares
-- them). A new setting: its key in the right list below. Saving this file reloads it;
-- open pages follow.
local M = {}

M.pages = {
  { id = "gameplay", title = "Gameplay", help = "Quality-of-life options and the game's own options.",
    rows = {
      "qol_weapon_slots",
      "qol_skip_charging", "qol_skip_charging_who", "qol_skip_charging_name",
      "qol_skip_charging_hold_mine", "qol_skip_charging_hold_enemy",
      "qol_fast_travel", "qol_fast_travel_cost", "qol_fast_travel_flat", "qol_fast_travel_scale",
      "qol_fast_travel_prompt",
      "gameplay_walls", "gameplay_walls_ghost",
      "skip_intro",
      -- the game's own options (saved with the game): the classic screen has its own rows for them
      "map", "ability_timing", "weapon_status", "armor_status", "cursor_memory", "information", "puzzle_mode",
    } },
  { id = "display", title = "Display", help = "Widescreen, scaling, the window and brightness.",
    rows = {
      "widescreen", "aspect", "filter", "interpolate", "present_rate", "window_mode", "brightness", "renderer",
    } },
  { id = "graphics", title = "Graphics", help = "Lighting, resolution, anti-aliasing and the 3D picture.",
    rows = {
      "lighting_preset", "dynamic_lights", "gi_method", "shadows", "haze", "ao",
      "internal_resolution", "aa", "msaa", "ssaa", "aa_downsample",
      "perspective_textures", "subpixel", "smooth_textures", "environment", "emissive", "dust",
    } },
  { id = "audio", title = "Audio", help = "Volume and the game's sound output.",
    rows = { "master_volume", "sound" } },
  { id = "interface", title = "Interface", help = "Menus, text, 2D upscaling and button prompts.",
    rows = {
      "ui_menus", "ui_menu_opacity", "ui_menu_tint", "ui_menu_tint_r", "ui_menu_tint_g", "ui_menu_tint_b",
      "ui_text", "ui_upscale", "ui_lines", "ui_outline_boost", "ui_bubbles",
      "button_prompts", "button_prompts_style", "button_prompts_scale", "ui_subpixel",
    } },
  { id = "controls", title = "Controls", help = "The right stick camera and vibration.",
    rows = {
      "camera_stick", "camera_stick_speed", "camera_stick_deadzone", "camera_stick_curve", "camera_stick_invert_x",
      "camera_stick_zoom", "camera_stick_invert_y", "camera_stick_zoom_speed", "camera_stick_collision",
      "camera_stick_r3_reset", "camera_stick_idle_reset", "camera_stick_keys", "reset_camera",
      "qol_fp_invert_y", "qol_map_invert", "qol_map_controls", "qol_confirm_cross", "qol_field_cross_attack",
      "vibration",
      -- the classic screen reaches Bindings from here; the modern list shows it as an entry
      { page = "bindings", where = "classic" },
    } },
  { id = "bindings", title = "Bindings", help = "Keys and gamepad buttons for each PlayStation button.",
    parent = "controls",
    rows = {
      "bind_circle", "bind_cross", "bind_triangle", "bind_square", "bind_l1", "bind_r1", "bind_l2", "bind_r2",
      "bind_start", "bind_select", "bind_l3", "bind_r3", "bind_up", "bind_down", "bind_left", "bind_right",
      "bind_travel", "bind_travel_clear", "bind_reset",
    } },
}

-- Defines one page (also from the REPL: M.define{ id = "gameplay", ... } redefines it live).
function M.define(page)
  return rood.menu.page(page)
end

for _, page in ipairs(M.pages) do
  M.define(page)
end

return M
