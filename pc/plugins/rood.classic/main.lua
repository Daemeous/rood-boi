-- rood.classic (shipped, on): the classic look (ui_menus 0) drawn from Lua (pc/docs/Extensions.md,
-- "The classic look in Lua"; plan pc/docs/plans/2026-10-20-classic-ui-in-lua.md).
--
-- The kinds: the timing word (status), the chain count (chain) and the status display's HP / MP / RISK
-- gauges with their digits and labels (gauges), and Ashley's body/damage silhouette (figure).
-- The status carousel (status_icons), BATTLE MODE notice (battle_mode), and ability_icons are Lua too.
-- Ability groups leave pc_weapon_slots_label's weapon-mode names/markers and side effects in C.
-- Each draw function below is a copy of the game's own, made of the game's own pieces (d.text_raw is
-- its raw printer, d.value its digits, d.bar its gauge), so by default it emits exactly the game's
-- packets: the parity gate
-- (pc/tests/classic_parity.py) compares them every frame. What this file may change is the look the
-- classic rule allows: typography (how ui_text sets the game's glyphs: `typeset`, `typography`) and
-- 16:9 placement (`place`); each deliberate difference is named in `look.overrides` (the gate's
-- waivers, pc/tests/classic/waivers.toml, must list the same).
--
-- Edit `look` and save: the running game reloads this file in about half a second. To have something
-- to look at, in the Lua console (F1, Window > Lua console), in the field with the modern UI off:
--   rood.dev.show("status", { timing = "success", variant = 5 })   WELL TIMED!, held until rood.dev.clear()
--   rood.dev.show("status", { timing = "too_slow" })               TOO SLOW!
--   rood.dev.show("chain", { count = 3 })                           3 CHAINS
-- (the gauges are up whenever the field is: M.look.gauges.place.dx = 6 moves them at the next frame)
-- The REPL (pc/tools/rood_repl.py --plugin rood.classic) has this file's module as M: M.look.typeset.margin = 4
-- shows on the held word at the next frame; saving the file brings the file's values back.
-- Turned off (the Lua console's Plugins list), broken, or a kind's hud_classic_<kind> setting off: the
-- game draws them itself (the fallback, typography from ui_text's defaults).
local M = { draw = {} }
local S = rood.settings.get

local function make_look()
  return {
    -- The timing word (BATTLE.PRG func_800958A4, the label loop's kind 4): the game's own strings ('$'
    -- centres the word on x; two-word phrases are padded with the font's narrow blanks), colours (PS1
    -- colour words: 0xBBGGRR, 0x80 full) and slide (pixels left of its place at ages 0..3 coming in,
    -- and from `slide_out` on going out, the steps backwards).
    status = {
      place = { anchor = "left", dx = 0, dy = 0 }, -- 16:9: at the screen's left edge (the game's hook's anchor)
      words = { too_fast = "$TOO   FAST!", fast = "$FAST!", slow = "$SLOW!", too_slow = "$TOO   SLOW!" },
      success = { "$COOL!", "$GOOD!", "$EXCELLENT!", "$RIGHT   ON!", "$PERFECT!", "$WELL   TIMED!", "$NICE!",
                  "$GREAT!" },
      colours = { too_fast = 0x808000, fast = 0x608020, success = 0x408040, slow = 0x208060, too_slow = 0x008080 },
      slide = { 64, 16, 4, 1 },
      slide_out = 42,
    },
    -- The chain count (func_80095A4C, kind 5): "3 CHAINS" ("1 CHAIN"), slid in only for the first.
    chain = {
      place = { anchor = "left", dx = 0, dy = 0 },
      one = "$1 CHAIN",
      many = "$%d CHAINS",
      colour = 0x1040A0,
      slide = { 64, 16, 4, 1 },
      slide_out = 87,
    },
    -- The status display's gauges (5BF94.c func_800CA2DC's _renderStatBar calls): HP, MP and RISK, each a
    -- bar, its digits (the current value large, the maximum small with a separator) and its label; with no
    -- risk, RISK ZERO. The game's places (D_800EBBDC, D_800EBBEC: x, y before the display's slide), labels
    -- (the field UI page's HP, MP, RISK: D_800EBBFC, D_800EBC00) and the steps between them as data; the
    -- slots (bytes from vs_scratch.unk4) the game links them into.
    gauges = {
      place = { anchor = "left", dx = 0, dy = 0 }, -- 16:9: the panel's left edge
      pools = {
        { pool = "hp", bar = { 10, 18 }, label = { 10, 10 }, sprite = { page_x = 768, page_y = 0, bits = 4,
          u = 144, v = 0, w = 16, h = 9, clut_x = 864, clut_y = 223 } },
        { pool = "mp", bar = { 80, 18 }, label = { 80, 10 }, sprite = { page_x = 768, page_y = 0, bits = 4,
          u = 160, v = 0, w = 18, h = 9, clut_x = 864, clut_y = 223 } },
        { pool = "risk", bar = { 10, 22 }, label = { 10, 26 }, sprite = { page_x = 768, page_y = 0, bits = 4,
          u = 180, v = 0, w = 26, h = 9, clut_x = 864, clut_y = 223 } },
      },
      -- (steps as the game writes them: packed x | y << 16, added to its packed place: x's carry reaches y)
      maximum = 56 | (-8 << 16),        -- the small maximum's rightmost digit from the bar's place
      separator = 0xFFF9 | (0xFFFE << 16), -- the large current value's from the pen the maximum leaves
      risk_value = 54 | (3 << 16),      -- RISK's value (no maximum) from its bar's place
      digit_step = 5,                   -- the small digits' advance (vs_battle_renderValue: 5 + its font)
      risk_max = 100,
      zero = { text = "RISK  ZERO", x = 12, y = 22, colour = 0x808080 },
      layers = { bars = -12, digits = -16 },
    },
    -- Ashley's silhouette: D_800EBCEC / D_800EBD14 / D_800EBD3C, in the game's limb order.
    -- Each row is {dx, dy, width, height, u, v}; stance 0 then 1. Colours are D_800EBC54,
    -- indexed by the game's read-only condition (critical, damaged, wounded, good, excellent).
    figure = {
      place = { anchor = "left", x = 8, y = 184, dx = 0, dy = 0 }, layer = -4,
      colours = { 0x180058, 0x004070, 0x087078, 0x186820, 0x687018 },
      stances = {
        { {8,8,12,20,16,184}, {24,8,12,18,32,160}, {16,0,12,12,0,160},
          {16,8,12,14,0,172}, {8,16,28,24,44,160} },
        { {0,8,18,20,12,160}, {24,8,16,22,32,184}, {16,0,12,12,0,160},
          {16,8,12,14,0,172}, {8,16,28,24,44,160} },
      },
      sprite = { page_x = 768, page_y = 0, bits = 4, clut_x = 832, clut_y = 223 },
    },
    ability_icons = {
      place = { anchor = "centre", dx = 0, dy = 0 }, layer = "unk8", right_x = 160,
      -- marker x/y, label x/y/w/h/u/v; IDs 0..3 right, 4..7 left.
      labels = {
        {77,101,82,99,47,9,52,101}, {53,115,-2,113,58,9,100,101},
        {87,113,92,111,52,9,0,101}, {63,127,7,125,58,9,160,101},
        {89,101,94,99,34,9,0,113}, {70,115,27,112,57,9,152,113},
        {98,113,103,111,45,9,104,113}, {80,127,16,124,67,9,36,113},
      },
      marker = { w = 6, h = 6, u = 192, v = 128 },
      face_xy = { 0x640041, 0x6D0038, 0x6D004A, 0x760041 },
      face = { size = 16, u_step = 16, v = 128 },
      cross = { x = 72, y = 102, w = 30, h = 30, u = 184, v = 160 },
      label_sprite = { page_x = 448, page_y = 256, bits = 4, clut_x = 976, clut_y = 220 },
      button_sprite = { page_x = 768, page_y = 0, bits = 4, clut_x = 944, clut_y = 223 },
      full_colour = 0x808080, semi_alpha = 0.5,
      -- C decides eligibility; its codes select independent presentation styles.
      availability = {
        [0x140] = { colour = 0x404040, alpha = 0.5 },
        [0x180] = { colour = 0x808080, alpha = 0.5 },
      },
    },
    battle_mode = {
      place = { anchor = "right", dx = 0, dy = 0 }, layer = -12,
      x = 236, y = -4, step_y = 3, colour = 0x404040, alpha = 0.5,
      sprite = { page_x = 448, page_y = 256, bits = 4, u = 64, v = 0,
        w = 73, h = 10, clut_x = 976, clut_y = 220 },
    },
    -- func_800C9CB4: clipped carousel cells and the count/selection indicators.
    status_icons = {
      place = { anchor = "left", dx = 0, dy = 0 }, layers = { icons = -4, indicators = -8 },
      x = 160, y = 8, cell = 16, supplemental_h = 8,
      supplemental_w = 8, supplemental_stride = 8,
      supplemental_alignment = 8, -- positive power of two: atlas clipping masks
      texels = { 0x1010,0x1020,0x1030,0x1040,0x1050,0x1060,0x1080,0x1090,
        0x10A0,0x10B0,0x10C0,0x10D0,0x10E0,0x10F0,0x2000,0x2010,
        0x30B8,0x30B8,0x30B8,0x30B8,0x3098,0x3098,0x3098,0x3098,
        0x30C8,0x30C8,0x30C8,0x30C8,0x30A8,0x30A8,0x30A8,0x30A8 },
      alternate_clut = 0x0F0F906A, cluts = { 0x37F8, 0x37F9 },
      supplemental_uv = 0x3068, supplemental_clut = 0x37FF,
      indicator = { x = 148, y = 8, step = 3, row_step = 3, size = 4,
        columns = 8, -- positive power of two; selection deliberately tests this bit (not row division)
        strip_padding = 1, strip_from = 3,
        dot_uv = 0x0C1A, strip_uv = 0x0C00, clut = 0x37F4 },
      sprite = { page_x = 768, page_y = 0, bits = 4, bgr = 0x808080 },
    },
    -- How ui_text sets both (with ui_text font; the packets stay the game's): the status style,
    -- typeset as one word centred where the game centres it.
    typeset = {
      style = "status",
      blanks = "one",        -- a run of the game's narrow blanks is one space ("each": a space for each)
      keep_on_screen = true, -- moved only as far as it takes to stay on the screen (WELL TIMED! starts at x 5)
      screen_w = 320,        -- the field's screen (16:9 anchors the HUD at the window's edges: the same holds)
      margin = S("ui_text_edge_margin"),   -- pixels beyond the outline kept from the edges
      word_space = S("ui_text_word_space"), -- the font's space times this
    },
    -- ui_text's styles for this run (rood.hud.typography: font, size, stroke, tracking, stretch, slant,
    -- embolden, ...; over the ui_text_<style>_* settings; empty: the settings'), and its tunables:
    typography = {
      status = {},
      -- the gauges' digits: the font's old-style figures (3 4 5 7 9 descend) rise together into the room
      -- above them, by this much of what their deepest descender lacks, before any is squeezed (playtest fix
      -- 1d2f20c5: 1, all it takes; 0 the old squeeze that flattened 5s and 7s)
      digits = { descender_lift = S("ui_text_descender_lift") },
    },
    -- The deliberate differences from the game's packets, per kind (the parity gate's waivers):
    -- typography changes no packet (ui_text restyles at raster time); a place with dx, dy or another
    -- anchor would be "place".
    overrides = { status = { "typography" }, chain = { "typography" }, gauges = { "typography" } },
  }
end

local SETTINGS = { "ui_text_edge_margin", "ui_text_word_space", "ui_text_descender_lift" }
local look
local chain_text = {} -- "$3 CHAINS" by count, made once (no string per frame)

local function refresh()
  look = make_look()
  M.look = look
  chain_text = {}
  for style, fields in pairs(look.typography) do
    rood.hud.typography(style, next(fields) and fields or nil)
  end
end
M.refresh = refresh
refresh()
for _, key in ipairs(SETTINGS) do rood.settings.on_change(key, refresh) end

-- x moved by a slide table: in at ages 0..3, out from `out` on (the game's steps backwards).
local function slid(x, age, slide, out, coming_in)
  if coming_in and age < 4 then return x - slide[age + 1] end
  if age >= out then
    local step = out + 3 - age
    if step >= 0 and step < 4 then return x - slide[step + 1] end
  end
  return x
end

-- status: func_800958A4 (x, y: its call's 44, 48)
M.draw.status = function(e, d)
  local s = look.status
  local text = e.timing == "success" and s.success[(e.variant or 0) + 1] or s.words[e.timing]
  if not text then return end
  d.anchor(s.place.anchor)
  local x = slid(e.call_x, e.age, s.slide, s.slide_out, true)
  d.text_raw(text, x + s.place.dx, e.call_y + s.place.dy, s.colours[e.timing], look.typeset)
end

-- chain: func_80095A4C (x, y: 44, 64)
M.draw.chain = function(e, d)
  local c = look.chain
  local text = chain_text[e.value]
  if not text then
    text = e.value == 1 and c.one or string.format(c.many, e.value)
    chain_text[e.value] = text
  end
  d.anchor(c.place.anchor)
  local x = slid(e.call_x, e.age, c.slide, c.slide_out, e.value == 1)
  d.text_raw(text, x + c.place.dx, e.call_y + c.place.dy, c.colour, look.typeset)
end

-- gauges: func_800CA2DC's _renderStatBar(0, 1, 2) and RISK ZERO. The game links each packet at the head of
-- its slot, so a slot draws in the reverse of the game's calls: the commands below go in drawing order
-- (RISK ZERO, then RISK, MP, HP: each its label, value, separator, maximum, bar).
local xy = rood.draw.xy
local function digits(v)
  local n = 1
  while v >= 10 do v, n = v // 10, n + 1 end
  return n
end
local function gauge(d, g, i, current, max, dy)
  local p = g.pools[i]
  if max == 0 then return end -- (the game draws nothing for a pool with no maximum)
  local bx, by = xy(p.bar[1] + g.place.dx, p.bar[2] + g.place.dy, dy << 16)
  local mx, my, sx, sy, cx, cy
  if i == 3 then
    cx, cy = xy(bx, by, g.risk_value)
  else
    mx, my = xy(bx, by, g.maximum)
    sx, sy = xy(mx, my, -digits(max) * g.digit_step) -- (the pen the small digits leave)
    cx, cy = xy(sx, sy, g.separator)
  end
  local lx, ly = xy(p.label[1] + g.place.dx, p.label[2] + g.place.dy, dy << 16)
  d.layer(g.layers.bars)
  d.sprite(lx, ly, p.sprite)
  d.layer(g.layers.digits)
  d.value(1, cx, cy, current)
  if i ~= 3 then
    d.layer(g.layers.bars)
    d.digit(2, sx, sy, 0)
    d.layer(g.layers.digits)
    d.value(0, mx, my, max)
  end
  d.layer(g.layers.bars)
  d.bar(bx, by, current, max, p.pool)
end
M.draw.gauges = function(e, d)
  local g = look.gauges
  local dy = -(e.slide >> 16) -- (the display slides up 8 pixels a step)
  d.anchor(g.place.anchor)
  if e.risk == 0 then
    d.layer(g.layers.bars)
    d.text_raw(g.zero.text, g.zero.x + g.place.dx, g.zero.y + g.place.dy + dy, g.zero.colour)
  else
    gauge(d, g, 3, e.risk, g.risk_max, dy)
  end
  gauge(d, g, 2, e.mp, e.max_mp, dy)
  gauge(d, g, 1, e.hp, e.max_hp, dy)
end

-- Commands are in drawing order: the game's last limb is at the head of the slot.
local limb_fields = { "limb_0_status", "limb_1_status", "limb_2_status", "limb_3_status", "limb_4_status" }
M.draw.figure = function(e, d)
  local f = look.figure
  d.anchor(f.place.anchor)
  d.layer(f.layer)
  for i = 5, 1, -1 do
    local p = f.stances[e.stance + 1][i]
    local s = f.sprite -- reused descriptor: draw copies its values, no allocation each frame
    s.w, s.h, s.u, s.v, s.bgr = p[3], p[4], p[5], p[6], f.colours[e[limb_fields[i]] + 1]
    local x, y = xy(f.place.x + f.place.dx, f.place.y + f.place.dy, p[1] | (p[2] << 16))
    d.sprite(x, y, s)
  end
end

-- Drawing order, not allocation order: reverse the game's calls within each OT slot.
local function status_sprite(d, s, x, y, w, h, uv, clut, set_page)
  local p = s.sprite
  p.set_page = set_page ~= false
  p.w, p.h, p.u, p.v = w, h, uv & 255, (uv >> 8) & 255
  p.clut_x, p.clut_y = (clut & 63) * 16, clut >> 6
  d.sprite(x + s.place.dx, y + s.place.dy, p)
end
local function status_cell(d, s, id, offset, side)
  if id == 255 then return end
  local uv, x, w = s.texels[id + 1], s.x + side * s.cell, s.cell
  if offset < 0 then uv, w = uv - offset, w + offset
  else x, w = x + offset, w - offset end
  local clut = s.cluts[((s.alternate_clut >> id) & 1) + 1]
  -- A clipped buff/debuff also has a stat glyph; emit it first (C links it last).
  local width, alignment = s.supplemental_w, s.supplemental_alignment
  if (id & 16) ~= 0 and (x & alignment) == 0 then
    local sx, sw = x, w
    local suv = (id & 3) * s.supplemental_stride + s.supplemental_uv
    if sw > width then
      if (sx & (alignment - 1)) ~= 0 then sx, sw = sx + width, sw - width
      else sx, sw = sx - width + sw, width end
    else suv = suv + width - sw end
    status_sprite(d, s, sx, s.y, sw, s.supplemental_h, suv, s.supplemental_clut)
  end
  status_sprite(d, s, x, s.y, w, s.cell, uv, clut)
end
M.draw.status_icons = function(e, d)
  local s, o = look.status_icons, e.offset
  d.anchor(s.place.anchor)
  d.layer(s.layers.icons)
  if e.slide == 0 then
    status_cell(d, s, e.previous, o, 0)
    status_cell(d, s, e.current, o + s.cell, 0)
    status_cell(d, s, e.current, o, 1)
    status_cell(d, s, e.next, o + s.cell, 1)
  end
  d.layer(s.layers.indicators)
  local p = s.indicator
  local x, y = xy(p.x, p.y, -e.slide)
  if e.count < p.strip_from then
    for i = e.count - 1, 0, -1 do
      status_sprite(d, s, x + i * p.step, y, p.size, p.size, p.dot_uv, p.clut)
    end
  else
    status_sprite(d, s, x, y, math.min(e.count, p.columns) * p.step + p.strip_padding, p.size, p.strip_uv, p.clut)
    if e.count > p.columns then
      status_sprite(d, s, x, y + p.row_step, (e.count - p.columns) * p.step + p.strip_padding, p.size, p.strip_uv, p.clut, false)
    end
    if e.selected ~= 255 then
      status_sprite(d, s, x + (e.selected & (p.columns - 1)) * p.step,
        y + ((e.selected & p.columns) == 0 and 0 or p.row_step), p.size, p.size, p.dot_uv, p.clut, false)
    end
  end
end

M.draw.battle_mode = function(e, d)
  local b = look.battle_mode
  d.anchor(b.place.anchor)
  d.layer(b.layer)
  -- The game adds sine>>5 to the packed BGR word, NOT to every channel.
  b.sprite.bgr = (b.colour + e.pulse) & 0xFFFFFF
  local x, y = xy(b.x, b.y + e.step * b.step_y, -e.slide)
  d.sprite(x + b.place.dx, y + b.place.dy, b.sprite, nil, b.alpha)
end

local function ability_sprite(d, a, sprite, x, y, w, h, u, v, colour, alpha)
  sprite.w, sprite.h, sprite.u, sprite.v, sprite.bgr = w, h, u, v, colour
  d.sprite(x + a.place.dx, y + a.place.dy, sprite, nil, alpha)
end
M.draw.ability_icons = function(e, d)
  local a, id, offset = look.ability_icons, e.group, e.offset
  d.anchor(a.place.anchor)
  d.layer(a.layer)
  if id == 8 then
    local c = a.cross
    local x, y = xy(c.x + offset, c.y, 0) -- C masks x; no carry into y
    ability_sprite(d, a, a.button_sprite, x, y, c.w, c.h, c.u, c.v, a.full_colour, a.semi_alpha)
    return
  end
  local xbase = id < 4 and a.right_x - offset or offset
  if id < 4 then
    local f, index = a.face, 4 - id
    local x, y = xy(0, 0, a.face_xy[index] + xbase) -- unlike labels, packed carry is intentional
    ability_sprite(d, a, a.button_sprite, x, y, f.size, f.size, (index - 1) * f.u_step, f.v, a.full_colour, 1)
  end
  if e.availability ~= 0 then
    local p, m = a.labels[id + 1], a.marker
    local style = a.availability[e.availability]
    local colour, alpha = style.colour, style.alpha
    local x, y = xy(xbase + p[3], p[4], 0)
    ability_sprite(d, a, a.label_sprite, x, y, p[5], p[6], p[7], p[8], colour, alpha)
    x, y = xy(xbase + p[1], p[2], 0)
    ability_sprite(d, a, a.label_sprite, x, y, m.w, m.h, m.u, m.v, colour, alpha)
  end
end

for _, kind in ipairs({ "status", "chain", "gauges", "figure", "status_icons", "battle_mode", "ability_icons" }) do
  rood.hud.claim(kind, function(e, d) return M.draw[kind](e, d) end, { flavour = "classic" })
end

return M
