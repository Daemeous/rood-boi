-- rood.craftbook: at a workshop, what the game's Combine screen can make from the
-- bag, in a small panel. Off by default: turn it on in the Lua console's Plugins
-- list (F1, Window > Lua console) or with RPC lua {action "enable", plugin
-- "rood.craftbook"}.
--
-- A template for the player state queries (pc/docs/Extensions.md "Player state"):
--   rood.game.workshop()   nil, or the workshop you stand in;
--   rood.game.craftable()  every combination it allows, each with the game's
--                          own result (MENUC's combine, run on copies);
--   rood.game.mode()       "field" when nothing else (a menu, a cutscene) is up.
-- Queries give the same read-only table until the next game vsync, so a plugin
-- can call them freely; this one still asks only twice a second and rebuilds
-- its lines only when the answer changed, so its draw function makes no garbage.

local M = {}

-- How it looks: plain data. Edit and save: the plugin reloads.
-- Two looks, as the player's UI setting: "classic" (the modern menus off: the
-- game's own window colours and the menus' font; the classic look may only
-- change typography) and "modern".
M.look = {
  x = 8, y = 42, w = 168,    -- the panel's top left and width (the picture's pixels, 240 high)
  pad = 4, line_h = 9,       -- inner margin, one line's height
  max_lines = 8,             -- more than this: "and N more"
  title = "Craftbook",
  none = "Nothing to combine here",
  more = "and %d more",
  classic = {
    panel = 0x101C48, panel2 = 0x04040C, alpha = 0.9, edge = 0x9098A8,
    title = { font = "menu", size = 8, color = 0xF0D070 },
    text = { font = "menu", size = 7, color = 0xFFFFFF },
    dim = { font = "menu", size = 6.5, color = 0xA0A8C0 },
  },
  modern = {
    panel = 0x0E1014, alpha = 0.8, edge = 0xC8A860,
    title = { font = "labels", size = 8, color = 0xE8D8A8 },
    text = { font = "labels", size = 7, color = 0xF0F0F0 },
    dim = { font = "labels", size = 6.5, color = 0xA0A0A0 },
  },
}

-- A setting of its own (Options > plugins, or rood.settings).
local show = rood.settings.define("show", {
  type = "boolean",
  label = "Show at workshops",
  help = "Lists what the Combine screen can make from your bag while you stand in a workshop.",
  default = true,
})

-- Styles are handles: made once, at load.
local styles = {}
for look, L in pairs { classic = M.look.classic, modern = M.look.modern } do
  styles[look] = { title = rood.draw.style(L.title), text = rood.draw.style(L.text), dim = rood.draw.style(L.dim) }
end

local craft = require("craft") -- the lines, a module of its own so the tests can call it

local state = { list = nil, lines = {}, title = M.look.title }

local function draw(d, view)
  if rood.game.mode() ~= "field" then return end -- not over a menu or a cutscene
  local look = view.look == "classic" and "classic" or "modern"
  local L, st, c = M.look[look], styles[look], M.look
  local lines = state.lines
  local shown = math.min(#lines, c.max_lines)
  local rows = 1 + math.max(shown, 1) + (#lines > shown and 1 or 0)
  local h = c.pad * 2 + rows * c.line_h
  if L.panel2 then
    d.gradient(c.x, c.y, c.w, h, L.panel, L.panel2, true, L.alpha)
  else
    d.rect(c.x, c.y, c.w, h, L.panel, L.alpha)
  end
  d.rect(c.x, c.y, c.w, 0.75, L.edge)
  d.rect(c.x, c.y + h - 0.75, c.w, 0.75, L.edge)
  local x, y, w = c.x + c.pad, c.y + c.pad, c.w - 2 * c.pad
  d.fit(st.title, state.title, x, y, w, c.line_h)
  if #lines == 0 then
    d.fit(st.dim, c.none, x, y + c.line_h, w, c.line_h)
  end
  for i = 1, shown do
    d.fit(st.text, lines[i], x, y + i * c.line_h, w, c.line_h)
  end
  if #lines > shown then
    d.fit(st.dim, state.more, x, y + (shown + 1) * c.line_h, w, c.line_h)
  end
end

-- Twice a second: the workshop and what it can make; the panel set while in one.
local drawing = false
local function refresh()
  local workshop = rood.settings.get(show) and rood.game.workshop()
  if not workshop then
    if drawing then rood.draw.panel(nil) end
    drawing, state.list = false, nil
    return
  end
  local list = rood.game.craftable()
  if list ~= state.list then
    state.list = list
    state.lines = craft.lines(list)
    state.title = M.look.title .. ": " .. workshop.name
    state.more = string.format(M.look.more, #state.lines - M.look.max_lines)
  end
  if not drawing then rood.draw.panel(draw) end
  drawing = true
end

rood.timer.every(0.5, refresh, { clock = "real" })
refresh()

return M
