-- rood.hud (shipped, on): the game's HUD elements drawn from Lua (pc/docs/Extensions.md, "HUD").
-- The game decides when and what; this file decides how they look. Edit `look` below and save:
-- the running game reloads it in about half a second. To have something to look at, in the Lua
-- console (F1, Window > Lua console), in the field:
--   rood.dev.show("status", { timing = "slow" })        SLOW!, held until rood.dev.clear()
--   rood.dev.show("chain", { count = 3 })               3 CHAINS
--   rood.dev.show("buff", { slot = "weapon", stat = "human", value = 1 })
--   rood.dev.show("damage", { value = 123 })            over Ashley ({ heal = true }, { pool = "mp" })
--   rood.dev.show("travel")                             the Travel prompt without a Circle of Magic
--   rood.dev.show("notice", { text = "Saved to slot 3", value = 3 })   the save state slots' line
-- With the modern look (ui_menus 1) the claims below draw them and the game's own drawing is
-- skipped; with the classic look the game draws them (only typography: look.status.classic), unless
-- hud_classic_override is on: then the same claims run in the classic flavour (e.flavour "classic":
-- the game's own printers, sprites and windows, drawn in its frame).
-- Turned off (the Lua console's Plugins list), everything is the game's and the C++ drawing.
-- The module (returned at the end; the REPL's M, pc/tools/rood_repl.py --plugin rood.hud): the look,
-- the styles, refresh and the draw functions. The claims call M.draw[kind], so a draw function
-- redefined there (M.draw.status = function(e, d) ... end) draws from the next frame; saving
-- this file brings its own back.
local M = { draw = {} }
local S = rood.settings.get

-- A settings colour (r, g, b, a in 0..1) as 0xRRGGBB and its alpha.
local function colour(c)
  local function byte(v) return math.floor(math.max(0, math.min(1, v)) * 255 + 0.5) end
  return (byte(c[1]) << 16) | (byte(c[2]) << 8) | byte(c[3]), math.max(0, math.min(1, c[4]))
end

-- The look, read again whenever one of the settings it uses changes (the dev tools' sliders).
-- Sizes are the game's pixels (its screen is 320 x 224 in the field) unless said otherwise.
local function make_look()
  local fill, fill_alpha = colour(S("qol_fast_travel_prompt_color"))
  local edge, edge_alpha = colour(S("qol_fast_travel_prompt_stroke_color"))
  local stretch = S("qol_fast_travel_prompt_stretch")
  return {
    -- SLOW!, COOL!, ... and 3 CHAINS: the status style's typography (ui_text_status_*), outlined,
    -- in the game's colour, where the game centres the word.
    words = {
      font = "status",
      cap = 7 * S("ui_text_status_size"), -- the original's capitals are 7 pixels high
      stretch = S("ui_text_status_stretch"),
      tracking = S("ui_text_status_tracking"),
      stroke = S("ui_text_status_stroke"),
      stroke_color = 0x000000,
      shade = 0.94, -- the game's colour as its glyphs' body ramp shows it on average
      nudge = 0.5,  -- the capitals' middle below the middle of the game's glyph boxes
    },
    -- The classic look (ui_menus 0) keeps the game's drawing: only the status style's
    -- typography may change here (ui_text font). Fields of ui_text's styles (size, stroke,
    -- tracking, stretch, font, ...) for this run, over the ui_text_status_* settings; empty:
    -- the settings' as they are.
    classic = {},
    -- An equipment's class or affinity change (WEAPON HUMAN +1): each word fitted inside the
    -- game's box for it, the outline kept in.
    buff = {
      words = { font = "labels", size = 12, stretch = S("ui_text_labels_stretch"), stroke = 1,
                stroke_color = 0x101010 },
      digits = { font = "digits", size = 12, stroke = 1, stroke_color = 0x101010 },
    },
    -- Damage and healing over an actor: each digit where the game bounces it (6 pixels apart,
    -- the rightmost highest); a stat's change (STR -2) with its word fitted in the icon's box.
    damage = {
      digits = { font = "digits", cap = 9.5, stroke = 1.25, stroke_color = 0x101010, align = "center",
                 anchor = "middle" },
      advance = 6,
      words = { font = "labels", size = 12, stroke = 1, stroke_color = 0x101010 },
      fitted_digits = { font = "digits", size = 14, stroke = 1, stroke_color = 0x101010 },
    },
    -- The Travel prompt: the icon and TRAVEL outlined, no plate, from the settings the C++
    -- prompt reads (sizes in the menus' layout units, 720 high).
    travel = {
      verb = "TRAVEL",
      font = "labels",
      size = S("qol_fast_travel_prompt_size") * S("qol_fast_travel_prompt_scale"),
      stroke = S("qol_fast_travel_prompt_stroke") * S("qol_fast_travel_prompt_scale"),
      stretch = stretch > 0 and stretch or S("ui_text_labels_stretch"),
      icon = S("ui_prompt_icon"),
      gap = S("ui_prompt_gap") * S("qol_fast_travel_prompt_scale"),
      tracking = S("ui_prompt_tracking"),
      color = fill, alpha = fill_alpha,
      stroke_color = edge, stroke_alpha = edge_alpha,
    },
    -- The save state slots' notice ("Saved to slot 3"): the HUD labels' typography outlined, its
    -- corner e.corner on the game's point (the top middle, below HP and MP; sizes in the menus'
    -- layout units, 720 high), fading out at the end.
    notice = {
      font = "labels",
      size = 26,
      stroke = 2.5,
      stretch = S("ui_text_labels_stretch"),
      color = 0xFFFFFF,
      stroke_color = 0x000000,
      fade = 30, -- the last vsyncs of its life
    },
  }
end

local SETTINGS = {
  "ui_text_status_size", "ui_text_status_stretch", "ui_text_status_tracking", "ui_text_status_stroke",
  "ui_text_labels_stretch", "qol_fast_travel_prompt_size", "qol_fast_travel_prompt_scale",
  "qol_fast_travel_prompt_stroke", "qol_fast_travel_prompt_stretch", "qol_fast_travel_prompt_color",
  "qol_fast_travel_prompt_stroke_color", "ui_prompt_icon", "ui_prompt_gap", "ui_prompt_tracking",
}

local d = rood.draw
local look, style = nil, {}
local travel_unit -- the travel styles' scale (the picture's size)
local notice_unit -- the notice style's

local function travel_styles(unit)
  local t = look.travel
  travel_unit = unit
  style.verb = d.style{ font = t.font, size = math.max(1, t.size * unit), stretch = t.stretch, tracking = t.tracking,
                        stroke = t.stroke * unit, stroke_color = t.stroke_color, stroke_alpha = t.stroke_alpha,
                        color = t.color, alpha = t.alpha }
  style.icon = d.style{ stroke = t.stroke * unit, stroke_color = t.stroke_color, stroke_alpha = t.stroke_alpha,
                        alpha = t.alpha }
end

local function notice_style(unit)
  local n = look.notice
  notice_unit = unit
  style.notice = d.style{ font = n.font, size = math.max(1, n.size * unit), stretch = n.stretch,
                          stroke = n.stroke * unit, stroke_color = n.stroke_color, color = n.color }
end

local function refresh()
  look = make_look()
  M.look = look
  local w = look.words
  style.word = d.style{ font = w.font, cap = w.cap, stretch = w.stretch, tracking = w.tracking, stroke = w.stroke,
                        stroke_color = w.stroke_color, align = "center", anchor = "middle" }
  style.buff_word = d.style(look.buff.words)
  style.buff_digits = d.style(look.buff.digits)
  style.damage = d.style(look.damage.digits)
  style.damage_word = d.style(look.damage.words)
  style.damage_digits = d.style(look.damage.fitted_digits)
  if travel_unit then travel_styles(travel_unit) end
  if notice_unit then notice_style(notice_unit) end
  rood.hud.typography("status", next(look.classic) and look.classic or nil)
end
M.style, M.refresh = style, refresh
refresh()
for _, key in ipairs(SETTINGS) do rood.settings.on_change(key, refresh) end

-- A colour's channels times k.
local function shaded(rgb, k)
  local r, g, b = rgb >> 16, (rgb >> 8) & 255, rgb & 255
  return (math.floor(r * k) << 16) | (math.floor(g * k) << 8) | math.floor(b * k)
end

-- status and chain: the word, centred where the game centres it
local function word(e, d)
  local shade = e.flavour == "classic" and 1 or look.words.shade -- (the game's printer shades its own glyphs)
  d.text(style.word, e.text, e.x, e.y + look.words.nudge, shaded(e.rgb, shade))
end
M.draw.status = word
M.draw.chain = word

-- buff: the equipment's word, the class's or affinity's, the sign, the digits, each in its box
M.draw.buff = function(e, d)
  local p = e.parts
  if not p then return end
  for i = 1, #p do
    local part = p[i]
    d.fit(i < 4 and style.buff_word or style.buff_digits, part.text, part.x, part.y, part.w, part.h, e.rgb)
  end
end

-- damage: the digits at the game's place and bounce (func_80094B0C: from the right, each one's
-- rise half the one before's); a stat's word, sign and digits in their boxes
M.draw.damage = function(e, d)
  local p = e.parts
  if e.pool == "stat" and p and #p == 3 then
    if p[1].text ~= "" then d.fit(style.damage_word, p[1].text, p[1].x, p[1].y, p[1].w, p[1].h, e.rgb) end
    d.fit(style.damage_word, p[2].text, p[2].x, p[2].y, p[2].w, p[2].h, e.rgb)
    d.fit(style.damage_digits, p[3].text, p[3].x, p[3].y, p[3].w, p[3].h, e.rgb)
    return
  end
  local n, step = #e.text, look.damage.advance
  local x, rise = e.x + n * step / 2 - 2, e.bounce -- the rightmost digit's middle
  for i = n, 1, -1 do
    d.text(style.damage, e.text:sub(i, i), x, e.y + rise, e.rgb)
    x, rise = x - step, rise < 0 and -((-rise) // 2) or rise // 2 -- (C halves toward 0)
  end
end

-- travel: the icon then the verb, laid out as the C++ prompt (fast_travel_prompt.cpp), its
-- corner `e.corner` (0..8) on the picture's point e.x, e.y
M.draw.travel = function(e, d)
  local t = look.travel
  if e.unit ~= travel_unit then travel_styles(e.unit) end
  local u = e.unit
  local size, stroke = t.size * u, t.stroke * u
  local icon, gap = size * t.icon, t.gap * u
  local gw = d.glyph_width(e.glyph, icon)
  if gw <= 0 then return end -- nothing bound to Travel on this device
  -- (the C++ layout spaces after the last letter too)
  local vw = d.measure(style.verb, t.verb) + t.tracking * size * t.stretch
  local w, h = stroke + gw + gap + vw + stroke, math.max(icon, size) + stroke * 2
  local x0 = e.x - w * (e.corner % 3) * 0.5
  local y0 = e.y - h * (e.corner // 3) * 0.5
  d.glyph(e.glyph, x0 + stroke, y0 + h * 0.5, icon, style.icon)
  d.text(style.verb, t.verb, x0 + stroke + gw + gap, y0 + (h - size) * 0.5)
end

-- notice: the save state slots' line, its corner e.corner (0..8) on e.x, e.y (the picture as 320 x 240)
M.draw.notice = function(e, d)
  if e.unit ~= notice_unit then notice_style(e.unit) end
  local w, h = d.measure(style.notice, e.text)
  local x0 = e.x - w * (e.corner % 3) * 0.5
  local y0 = e.y - h * (e.corner // 3) * 0.5
  local left = e.life - e.age
  local alpha = left < look.notice.fade and math.max(0, left / look.notice.fade) or 1
  d.text(style.notice, e.text, x0, y0, look.notice.color, alpha)
end

for _, kind in ipairs({ "status", "chain", "buff", "damage", "travel", "notice" }) do
  rood.hud.claim(kind, function(e, d) return M.draw[kind](e, d) end)
end

return M
