-- rood.photo's look, feel and choices (main.lua uses them; tests/photo_test.lua checks them). Edit and
-- save: the running game reloads the plugin (leaving the photo mode if it is open).
local M = {}

-- ---------------------------------------------------------------- the look (sizes in the picture's
-- square pixels, 240 high: rood.draw.panel)
M.look = {
  title = "Settings",
  card = { x = 10, y = 12, w = 150, row_h = 13, pad = 6, title_h = 16, hint_h = 11, bar_w = 44, bar_h = 3 },
  -- the modern look (ui_menus 1)
  modern = {
    panel = 0x0E1116, panel_alpha = 0.82, edge = 0x3A414C, edge_alpha = 0.9,
    selected = 0xD9B45A, selected_alpha = 0.22,
    text = { font = "labels", size = 8.5, color = 0xE6E6E6, stroke = 0.6, stroke_color = 0x000000 },
    value = { font = "labels", size = 8.5, color = 0xFFFFFF, align = "right", stroke = 0.6, stroke_color = 0x000000 },
    title = { font = "labels", size = 10, color = 0xD9B45A, stroke = 0.6, stroke_color = 0x000000 },
    hint = { font = "labels", size = 7, color = 0xB8B8B8, stroke = 0.5, stroke_color = 0x000000 },
    dim = 0x707070, bar = 0x3A414C, fill = 0xD9B45A,
    glyphs = true, -- button prompt icons in the hints (the button prompts setting's)
  },
  -- the classic look (ui_menus 0): the game's window colours and its menu font (only the typography is
  -- ui_text's); no prompt icons (the game's own button sprites are not drawn over the picture): words
  classic = {
    panel = 0x101C48, panel2 = 0x04040C, panel_alpha = 0.9, edge = 0x9098A8, edge_alpha = 1,
    selected = 0xF0D070, selected_alpha = 0.18,
    text = { font = "menu", size = 9, color = 0xFFFFFF },
    value = { font = "menu", size = 9, color = 0xF0D070, align = "right" },
    title = { font = "menu", size = 10, color = 0xF0D070 },
    hint = { font = "menu", size = 7.5, color = 0xC0C0C0 },
    dim = 0x808080, bar = 0x303850, fill = 0xF0D070,
    glyphs = false,
  },
  -- the brief notices ("Saved ...", why the photo mode cannot open here)
  notice = { style = { font = "labels", size = 9, color = 0xFFFFFF, stroke = 1, stroke_color = 0x000000,
                       align = "center" },
             y = 214, seconds = 2.5, fade = 0.5 },
  -- the rule of thirds (the setting "Thirds grid")
  grid = { color = 0xFFFFFF, alpha = 0.35, width = 0.5 },
  hints = {
    { "circle", "Take photo" }, { "cross", "Exit" }, { "triangle", "Hide card" }, { "square", "Reset row" },
  },
}

-- ---------------------------------------------------------------- the camera's feel
M.camera = {
  orbit = 90,         -- degrees a second, a stick fully over (yaw; pitch the same)
  move = 900,         -- world units a second (Ashley is about 180 high)
  rise = 600,         -- L1 / R1: down / up, world units a second
  zoom = 1.2,         -- L2 / R2: the distance to the pivot, e-folds a second
  deadzone = 0.15,
  radius = 3000,      -- how far the pivot may go from where it began (world units)
  collision = false,  -- true: the eye kept out of the room's walls (the game's own camera often looks through them)
  -- keys (rood.input's key names), held: the camera; the PS1 buttons' keys (arrows, Z, X, S, A, Q, W, 1, 2)
  -- do what their buttons do
  keys = {
    forward = "i", back = "k", left = "j", right = "l", down = "u", up = "o",
    yaw_left = "f", yaw_right = "h", pitch_up = "t", pitch_down = "g", zoom_in = "c", zoom_out = "v",
  },
  repeat_delay = 0.35, repeat_every = 0.06, -- a held left / right on a slider
}

-- ---------------------------------------------------------------- the rows' choices
-- Format: the picture cropped to an aspect ratio (width over height; 0: the whole picture).
M.formats = {
  { label = "Fullscreen", aspect = 0 },
  { label = "4:3", aspect = 4 / 3 },
  { label = "3:2", aspect = 3 / 2 },
  { label = "16:9", aspect = 16 / 9 },
  { label = "Cinema 2.39:1", aspect = 2.39 },
  { label = "Square", aspect = 1 },
  { label = "Portrait 4:5", aspect = 4 / 5 },
  { label = "Vertical 9:16", aspect = 9 / 16 },
}

-- Filter: Off, the post presets (pc/assets/post, pc/user/post: added at load, after these), and grades as
-- post values (keys the other rows never set).
M.filters = {
  { label = "Off" },
  { label = "Noir", values = { saturation = { 1, 1, 1, 0 }, contrast = { 1, 1, 1, 1.25 } } },
  { label = "Sepia", values = { saturation = { 1, 1, 1, 0.15 }, temperature = 4300,
                                midtones_gain = { 1.06, 0.98, 0.86, 1 } } },
  { label = "Faded", values = { saturation = { 1, 1, 1, 0.75 }, contrast = { 1, 1, 1, 0.85 },
                                shadows_offset = { 0.02, 0.02, 0.025, 0 } } },
  { label = "Vivid", values = { saturation = { 1, 1, 1, 1.35 }, contrast = { 1, 1, 1, 1.1 } } },
  { label = "Warm", values = { temperature = 4800, highlights_gain = { 1.05, 1, 0.92, 1 } } },
  { label = "Cold", values = { temperature = 9000, saturation = { 1, 1, 1, 0.85 } } },
}
M.preset_labels = { film = "Film", cinematic = "Cinematic", clean_modern = "Clean" }

-- Aperture: Off (no depth of field), or the lens's f-number (the physical depth of field).
M.apertures = { 0, 1.4, 2, 2.8, 4, 5.6, 8, 11, 16, 22 }
-- Focus: where the depth of field focuses (post dof_focus).
M.focus = {
  { label = "Subject", value = "subject" }, { label = "Centre", value = "centre" },
  { label = "Cursor", value = "cursor" }, { label = "Ashley", value = "ashley" },
}

-- The card's rows. kind "choice": left / right step through `choices` (wrapping); "slider": `min`..`max`
-- by `step`, the post key's live value at first. There is no Pose row: the game has no pose system,
-- and Ashley's animation is game state (a paused moment is rewound after every render), so changing it
-- is not a read-only thing (pc/docs/Extensions.md "Photo mode").
M.rows = {
  { id = "format", label = "Format", kind = "choice" },
  { id = "filter", label = "Filter", kind = "choice" },
  { id = "vignette", label = "Vignette", kind = "slider", key = "vignette", min = 0, max = 1, step = 0.05 },
  { id = "grain", label = "Grain", kind = "slider", key = "grain", min = 0, max = 1, step = 0.05 },
  { id = "aberration", label = "Aberration", kind = "slider", key = "chromatic_aberration", min = 0, max = 2,
    step = 0.1 },
  { id = "exposure", label = "Exposure", kind = "slider", key = "exposure", min = -3, max = 3, step = 0.25,
    format = "%+.2f EV" },
  { id = "aperture", label = "Aperture", kind = "choice" },
  { id = "focus", label = "Focus", kind = "choice" },
}

-- Where the photo mode may open: the field and battles (not menus, cutscenes, loading, the title).
local WHY = {
  menu = "Photo mode: not in a menu", cutscene = "Photo mode: not during a scene",
  title = "Photo mode: not on the title screen", attract = "Photo mode: not during the demo",
  none = "Photo mode: not while loading",
}
function M.allowed(mode)
  if mode == "field" or mode == "battle" then return true end
  return false, WHY[mode] or ("Photo mode: not here (" .. tostring(mode) .. ")")
end

return M
