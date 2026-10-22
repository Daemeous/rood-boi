-- rood.photo's look, feel, tabs and choices (main.lua uses them; tests/photo_test.lua checks them). Edit and
-- save: the running game reloads the plugin (leaving the photo mode if it is open).
local M = {}

-- ---------------------------------------------------------------- the look (sizes in the picture's
-- square pixels, 240 high: rood.draw.panel)
M.look = {
  title = "Photo mode",
  card = { x = 6, y = 8, w = 152, title_h = 13, tab_h = 11, row_h = 10, pad = 4, hint_h = 9, bar_w = 30,
           bar_h = 3, rows_max = 13, -- rows shown at once (more scroll)
           label_share = 0.46 },      -- the labels' share of the width (the values take the rest)
  -- the modern look (ui_menus 1)
  modern = {
    panel = 0x0E1116, panel_alpha = 0.82, edge = 0x3A414C, edge_alpha = 0.9,
    selected = 0xD9B45A, selected_alpha = 0.22, tab = 0x1C222B, tab_on = 0xD9B45A, tab_on_alpha = 0.3,
    text = { font = "labels", size = 7, color = 0xE6E6E6, stroke = 0.6, stroke_color = 0x000000 },
    value = { font = "labels", size = 7, color = 0xFFFFFF, align = "right", stroke = 0.6, stroke_color = 0x000000 },
    title = { font = "labels", size = 8.5, color = 0xD9B45A, stroke = 0.6, stroke_color = 0x000000 },
    tabs = { font = "labels", size = 6.5, color = 0xB8B8B8, align = "center", stroke = 0.5, stroke_color = 0x000000 },
    tab_lit = { font = "labels", size = 6.5, color = 0xFFFFFF, align = "center", stroke = 0.5, stroke_color = 0x000000 },
    hint = { font = "labels", size = 6, color = 0xB8B8B8, stroke = 0.5, stroke_color = 0x000000 },
    dim = 0x707070, bar = 0x3A414C, fill = 0xD9B45A, scroll = 0x8A919C,
    glyphs = true, -- button prompt icons in the hints (the button prompts setting's)
  },
  -- the classic look (ui_menus 0): the game's window colours and its menu font (only the typography is
  -- ui_text's); no prompt icons (the game's own button sprites are not drawn over the picture): words
  classic = {
    panel = 0x101C48, panel2 = 0x04040C, panel_alpha = 0.9, edge = 0x9098A8, edge_alpha = 1,
    selected = 0xF0D070, selected_alpha = 0.18, tab = 0x18245A, tab_on = 0xF0D070, tab_on_alpha = 0.25,
    text = { font = "menu", size = 7.5, color = 0xFFFFFF },
    value = { font = "menu", size = 7.5, color = 0xF0D070, align = "right" },
    title = { font = "menu", size = 8.5, color = 0xF0D070 },
    tabs = { font = "menu", size = 6.5, color = 0xA0A8C0, align = "center" },
    tab_lit = { font = "menu", size = 6.5, color = 0xFFFFFF, align = "center" },
    hint = { font = "menu", size = 6, color = 0xC0C0C0 },
    dim = 0x808080, bar = 0x303850, fill = 0xF0D070, scroll = 0x9098A8,
    glyphs = false,
  },
  -- the brief notices ("Saved ...", why the photo mode cannot open here)
  notice = { style = { font = "labels", size = 9, color = 0xFFFFFF, stroke = 1, stroke_color = 0x000000,
                       align = "center" },
             y = 214, seconds = 2.5, fade = 0.5 },
  recovered_notice = "Photo mode was restarted after an error",
  -- the guides (Frame > Guides), never in the photo
  grid = { color = 0xFFFFFF, alpha = 0.35, width = 0.5 },
  -- the hints under the rows: { buttons, text } (one button or two)
  hints = {
    { { "l1", "r1" }, "Tab" }, { "square", "Reset" }, { "circle", "Take photo" },
    { "cross", "Exit" }, { "triangle", "Hide" }, { "select", "Reset all" },
  },
  hint_columns = 3,
  -- the Presets tab's hints, in place of a row's value
  preset_empty = "Empty", preset_hint = "< Save   Load >",
}

-- ---------------------------------------------------------------- the sound while the game is held (the
-- setting Music: keep, duck, stop)
M.audio = { duck = 0.35 } -- Music "duck": the gain (0..1) over the master volume

-- ---------------------------------------------------------------- the camera's feel
M.camera = {
  orbit = 90,         -- degrees a second, a stick fully over (yaw; pitch the same; the free look too)
  move = 900,         -- world units a second (Ashley is about 180 high)
  rise = 600,         -- L2 / R2: down / up, world units a second
  zoom = 1.2,         -- orbit: the distance to the pivot, e-folds a second (the left stick's up / down)
  deadzone = 0.15,
  radius = 3000,      -- how far the pivot may go from where it began (world units)
  collision = false,  -- the Collision row's first value (the game's own camera often looks through walls)
  sensor = 24,        -- mm, the picture's height on the sensor: the field of view shown as a focal length
  -- keys (rood.input's key names), held: the camera; the PS1 buttons' keys (arrows, Z, X, S, A, Q, W, 1, 2,
  -- Tab) do what their buttons do
  keys = {
    forward = "i", back = "k", left = "j", right = "l", down = "u", up = "o",
    yaw_left = "f", yaw_right = "h", pitch_up = "t", pitch_down = "g", zoom_in = "c", zoom_out = "v",
    next_tab = "e", reset_tab = "r",
  },
  repeat_delay = 0.35, repeat_every = 0.06, -- a held left / right on a row
}

-- The card hidden (Triangle): the PS1 buttons that still act (show the card, a photo, leave); the others
-- (the D-pad, L1 / R1, Square ...) do nothing then, so the card's last row never changes unseen.
M.hidden_buttons = { triangle = true, circle = true, cross = true }

-- ---------------------------------------------------------------- the rows' choices
-- Format: the picture cropped to an aspect ratio (width over height; 0: the whole picture).
M.formats = {
  { label = "Fullscreen", value = 0 },
  { label = "4:3", value = 4 / 3 },
  { label = "3:2", value = 3 / 2 },
  { label = "16:9", value = 16 / 9 },
  { label = "1.85:1", value = 1.85 },
  { label = "Cinema 2.39:1", value = 2.39 },
  { label = "Square", value = 1 },
  { label = "Portrait 4:5", value = 4 / 5 },
  { label = "Vertical 9:16", value = 9 / 16 },
}

-- Filter: Off, grades as post values (blended in by Filter strength), then the post presets
-- (pc/assets/post, pc/user/post: added at load; whole looks, strength does not apply).
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
  { label = "Bleach", values = { saturation = { 1, 1, 1, 0.45 }, contrast = { 1, 1, 1, 1.3 },
                                 highlights_gain = { 1.04, 1.04, 1.04, 1 } } },
  { label = "Teal & orange", values = { shadows_gain = { 0.9, 1, 1.08, 1 }, highlights_gain = { 1.08, 1, 0.9, 1 },
                                        saturation = { 1, 1, 1, 1.1 } } },
}
M.preset_labels = { film = "Film", cinematic = "Cinematic", clean_modern = "Clean" }

-- Aperture (the physical model's f-number): the stops left / right step through.
M.apertures = { 1.2, 1.4, 2, 2.8, 4, 5.6, 8, 11, 16, 22 }
-- Focus: where the depth of field focuses (post dof_focus).
M.focus = {
  { label = "Camera subject", value = "subject" }, { label = "On Ashley", value = "ashley" },
  { label = "Screen centre", value = "centre" }, { label = "Cursor", value = "cursor" },
  { label = "Manual", value = "manual" },
}
M.dof_models = { { label = "Physical", value = "physical" }, { label = "Artistic", value = "artistic" } }
M.bokeh_edges = { { label = "Soft", value = "soft" }, { label = "Even", value = "even" }, { label = "Rim", value = "rim" } }
M.camera_modes = { { label = "Orbit", value = "orbit" }, { label = "Free", value = "free" } }
-- Guides (Frame): lines over the view while framing (never in the photo).
M.guides = {
  { label = "Off", value = "off" }, { label = "Thirds", value = "thirds" }, { label = "Grid 4x4", value = "grid" },
  { label = "Golden ratio", value = "golden" }, { label = "Centre", value = "centre" },
}
-- Bars (Frame): the colour of the format's bars over the view (the photo is cropped to the format).
M.bars = {
  { label = "Black", value = 0x000000 }, { label = "White", value = 0xFFFFFF }, { label = "Grey", value = 0x808080 },
  { label = "Ink", value = 0x101820 },
}

-- ---------------------------------------------------------------- the card's tabs and rows
-- A row: id, label, kind (slider, choice, toggle, action) and what it changes:
--   post = key (rood.post; `component` 1-4 of a colour / vec4 key, 4 the master), camera = fov / roll /
--   collision (rood.camera), setting = a setting of this plugin's, state = mode / guides / bars / filter /
--   filter_amount / show_ashley / show_others (the plugin's own; `default` its first value, else 1).
-- Sliders: min, max, step (a held left / right repeats), fmt (string.format of the value times `scale`), or
-- show = a unit main.lua formats (focal: a field of view as a focal length; metres: a GTE depth; percent).
-- Choices: choices = a list above (by name), its items { label, value }. needs = { id, value }: the row is
-- dimmed (and left / right do nothing) unless that row has that value (true: a toggle on); needs_post = key:
-- unless that post value is set (a LUT file); needs_grade: unless the filter is a grade (not Off, a preset).
-- There is no Pose row: the game has no pose system, and Ashley's animation is game state (a paused moment
-- is rewound after every render), so changing it is not read-only. Show Ashley / Show others: the
-- characters left out of the renders (rood.ui.hide{actors}: their models, weapons and drop shadows).
M.tabs = {
  { id = "camera", label = "Camera", rows = {
    { id = "mode", label = "Camera", kind = "choice", state = "mode", choices = "camera_modes" },
    { id = "fov", label = "Field of view", kind = "slider", camera = "fov", min = 10, max = 110, step = 1, show = "focal" },
    { id = "roll", label = "Roll", kind = "slider", camera = "roll", min = -45, max = 45, step = 1, fmt = "%+.0f\u{B0}" },
    { id = "speed", label = "Move speed", kind = "slider", setting = "camera_speed", min = 0.25, max = 4, step = 0.25,
      fmt = "%.2fx" },
    { id = "collision", label = "Collision", kind = "toggle", camera = "collision" },
    { id = "show_ashley", label = "Show Ashley", kind = "toggle", state = "show_ashley", default = true },
    { id = "show_others", label = "Show others", kind = "toggle", state = "show_others", default = true },
    { id = "reset_camera", label = "Reset camera", kind = "action", action = "reset_camera" },
  } },
  { id = "lens", label = "Lens", rows = {
    { id = "dof", label = "Depth of field", kind = "toggle", post = "dof" },
    { id = "focus", label = "Focus", kind = "choice", post = "dof_focus", choices = "focus", needs = { "dof", true } },
    { id = "focus_distance", label = "Focus distance", kind = "slider", post = "dof_focus_depth", min = 64, max = 8000,
      step = 32, show = "metres", needs = { "focus", "manual" } },
    { id = "model", label = "Model", kind = "choice", post = "dof_model", choices = "dof_models", needs = { "dof", true } },
    { id = "fstop", label = "Aperture", kind = "slider", post = "dof_fstop", stops = "apertures", fmt = "f/%.1f",
      needs = { "model", "physical" } },
    { id = "aperture", label = "Blur amount", kind = "slider", post = "dof_aperture", min = 0.25, max = 16, step = 0.25,
      fmt = "%.2f", needs = { "model", "artistic" } },
    { id = "focal", label = "Lens", kind = "slider", post = "dof_focal_length", min = 0, max = 300, step = 5,
      fmt = "%.0f mm", zero = "From FOV", needs = { "model", "physical" } },
    { id = "far_blur", label = "Max blur", kind = "slider", post = "dof_max_blur", min = 0, max = 32, step = 1,
      fmt = "%.0f px", needs = { "dof", true } },
    { id = "near_blur", label = "Near blur", kind = "slider", post = "dof_near_blur", min = 0, max = 1, step = 0.05,
      show = "percent", needs = { "dof", true } },
    { id = "blades", label = "Bokeh blades", kind = "slider", post = "bokeh_blades", min = 0, max = 9, step = 1,
      fmt = "%.0f", zero = "Round", needs = { "dof", true } },
    { id = "curvature", label = "Blade curve", kind = "slider", post = "bokeh_curvature", min = 0, max = 1, step = 0.05,
      show = "percent", needs = { "dof", true } },
    { id = "rotation", label = "Bokeh rotation", kind = "slider", post = "bokeh_rotation", min = 0, max = 360, step = 5,
      fmt = "%.0f\u{B0}", needs = { "dof", true } },
    { id = "squeeze", label = "Anamorphic", kind = "slider", post = "bokeh_squeeze", min = 1, max = 2, step = 0.05,
      fmt = "%.2fx", needs = { "dof", true } },
    { id = "edge", label = "Bokeh edge", kind = "choice", post = "bokeh_edge", choices = "bokeh_edges",
      needs = { "dof", true } },
    { id = "highlights", label = "Highlights", kind = "slider", post = "bokeh_highlights", min = 0, max = 2,
      step = 0.1, fmt = "%.1fx", needs = { "dof", true } },
    { id = "fringe", label = "Bokeh fringe", kind = "slider", post = "bokeh_fringe", min = 0, max = 1, step = 0.05,
      show = "percent", needs = { "dof", true } },
    { id = "cats_eye", label = "Cat's eye", kind = "slider", post = "bokeh_cats_eye", min = 0, max = 1, step = 0.05,
      show = "percent", needs = { "dof", true } },
  } },
  { id = "effects", label = "Effects", rows = {
    { id = "vignette", label = "Vignette", kind = "slider", post = "vignette", min = 0, max = 1, step = 0.05,
      show = "percent" },
    { id = "vignette_falloff", label = "Vignette size", kind = "slider", post = "vignette_gain", min = 0.25, max = 4,
      step = 0.25, fmt = "%.2fx" },
    { id = "vignette_round", label = "Vignette shape", kind = "slider", post = "vignette_roundness", min = 0, max = 1,
      step = 0.1, show = "percent" },
    { id = "distortion", label = "Lens distortion", kind = "slider", post = "lens_distortion", min = -0.5, max = 0.5,
      step = 0.05, fmt = "%+.2f" },
    { id = "grain", label = "Grain", kind = "slider", post = "grain", min = 0, max = 1, step = 0.05, show = "percent" },
    { id = "grain_size", label = "Grain size", kind = "slider", post = "grain_size", min = 0.5, max = 4, step = 0.25,
      fmt = "%.2f px" },
    { id = "grain_colour", label = "Grain colour", kind = "slider", post = "grain_color", min = 0, max = 1, step = 0.1,
      show = "percent" },
    { id = "aberration", label = "Aberration", kind = "slider", post = "chromatic_aberration", min = 0, max = 5,
      step = 0.1, fmt = "%.1f px" },
    { id = "bloom", label = "Bloom", kind = "slider", post = "bloom_intensity", min = 0, max = 4, step = 0.1,
      fmt = "%.1f" },
    { id = "bloom_threshold", label = "Bloom threshold", kind = "slider", post = "bloom_threshold", min = 0, max = 4,
      step = 0.1, fmt = "%.1f" },
    { id = "bloom_size", label = "Bloom size", kind = "slider", post = "bloom_size", min = 0.25, max = 4,
      step = 0.25, fmt = "%.2fx" },
    { id = "sharpen", label = "Sharpen", kind = "slider", post = "sharpen", min = 0, max = 1, step = 0.05,
      show = "percent" },
    { id = "tilt_shift", label = "Tilt-shift", kind = "toggle", post = "tilt_shift" },
    { id = "tilt_centre", label = "Tilt band", kind = "slider", post = "tilt_centre", min = 0, max = 1, step = 0.05,
      show = "percent", needs = { "tilt_shift", true } },
    { id = "tilt_width", label = "Tilt width", kind = "slider", post = "tilt_width", min = 0, max = 0.5, step = 0.02,
      show = "percent", needs = { "tilt_shift", true } },
    { id = "tilt_blur", label = "Tilt blur", kind = "slider", post = "tilt_blur", min = 0, max = 16, step = 0.5,
      fmt = "%.1f px", needs = { "tilt_shift", true } },
  } },
  { id = "colour", label = "Colour", rows = {
    { id = "filter", label = "Filter", kind = "choice", state = "filter", choices = "filters" },
    { id = "filter_amount", label = "Filter strength", kind = "slider", state = "filter_amount", min = 0, max = 1,
      step = 0.05, show = "percent", default = 1, needs_grade = true },
    { id = "exposure", label = "Exposure", kind = "slider", post = "exposure", min = -4, max = 4, step = 0.25,
      fmt = "%+.2f EV" },
    { id = "contrast", label = "Contrast", kind = "slider", post = "contrast", component = 4, min = 0.5, max = 1.5,
      step = 0.05, show = "percent" },
    { id = "saturation", label = "Saturation", kind = "slider", post = "saturation", component = 4, min = 0, max = 2,
      step = 0.05, show = "percent" },
    { id = "temperature", label = "Temperature", kind = "slider", post = "temperature", min = 1500, max = 15000,
      step = 250, fmt = "%.0f K" },
    { id = "tint", label = "Tint", kind = "slider", post = "tint", min = -1, max = 1, step = 0.05, fmt = "%+.2f" },
    { id = "lut_intensity", label = "LUT strength", kind = "slider", post = "lut_intensity", min = 0, max = 1,
      step = 0.05, show = "percent", needs_post = "lut" },
  } },
  { id = "frame", label = "Frame", rows = {
    { id = "format", label = "Format", kind = "choice", post = "format", choices = "formats" },
    { id = "bars", label = "Bars", kind = "choice", state = "bars", choices = "bars" },
    { id = "guides", label = "Guides", kind = "choice", state = "guides", choices = "guides" },
  } },
  { id = "presets", label = "Presets", presets = 6 }, -- rows: the slots (plugin storage)
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
