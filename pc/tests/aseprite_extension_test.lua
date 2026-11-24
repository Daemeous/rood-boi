-- The Aseprite extension's texel locator without Aseprite (pc/tests/aseprite_test.py runs this under lua5.4):
--   lua5.4 aseprite_extension_test.lua pc/tools/aseprite/rood-live/rood-live.lua
-- Aseprite's API is faked just far enough: a sprite of a rood package, app.editor.spritePos (the cursor's pixel), a Timer that is ticked by hand
-- and a WebSocket that records what is sent. The extension must send the cursor's pixel once per change (hover), nothing outside the canvas or
-- with hover off, and select (and, for a click, scroll to) the texel the studio picks.
local extension = arg[1]
local failures = 0
local function check(ok, message)
  print((ok and "ok:   " or "FAIL: ") .. message)
  if not ok then failures = failures + 1 end
end

local timers, sockets = {}, {}
Timer = function(t)
  local o = { interval = t.interval, ontick = t.ontick }
  function o:start() end
  function o:stop() end
  timers[#timers + 1] = o
  return o
end
WebSocketMessageType = { OPEN = 1, TEXT = 2, BINARY = 3, CLOSE = 4 }
WebSocket = function(t)
  local o = { onreceive = t.onreceive, text = {}, bin = {} }
  function o:connect() end
  function o:close() end
  function o:sendText(x) self.text[#self.text + 1] = x end
  function o:sendBinary(x) self.bin[#self.bin + 1] = x end
  sockets[#sockets + 1] = o
  return o
end
Dialog = function() return setmetatable({}, { __index = function() return function(self) return self end end }) end
Color = function(t) return t end
Rectangle = function(x, y, w, h) return { x = x, y = y, width = w, height = h } end
ColorMode = { RGB = 1 }

local selection = {}
function selection:select(r) self.selected = r end
local function sprite_of(title)
  return { colorMode = 1, width = 256, height = 256, filename = "/x/" .. title .. ".aseprite", selection = selection,
           layers = { { name = "Game colour (reference)", isGroup = false }, { name = "Height", isGroup = false } },
           events = { on = function() end } }
end
local sprite, other = sprite_of("m0123456789_p10_c3b70"), sprite_of("m0123456789_p11_c0000")
local editor = { sprite = sprite, spritePos = { x = 3, y = 4 }, scroll = { x = 0, y = 0 } }
app = { sprite = sprite, sprites = { sprite, other }, editor = editor, refresh = function() end,
        fs = { fileTitle = function(p) return p:match("([^/]+)%.aseprite$") end },
        events = { on = function() end }, fgColor = { red = 0, green = 0, blue = 0 } }

dofile(extension)
local prefs = {}
local commands = {}
init({ preferences = prefs, newCommand = function(_, c) commands[c.id] = c end })
check(#sockets == 1, "the extension connects to the bridge for a rood sprite")
local ws = sockets[1]
local function hover_timer() for _, t in ipairs(timers) do if t.interval == 0.05 then return t end end end
check(hover_timer() ~= nil, "a hover timer reads the cursor's pixel")

ws.onreceive(WebSocketMessageType.OPEN, "")
hover_timer().ontick()
local locates = {}
for _, t in ipairs(ws.text) do if t:find('"locate"', 1, true) then locates[#locates + 1] = t end end
check(#locates == 1 and locates[1] == '{"locate":{"material":"m0123456789_p10_c3b70","x":3,"y":4}}', "the cursor's pixel goes to the bridge as the texel of the sprite: " .. tostring(locates[1]))
hover_timer().ontick()
hover_timer().ontick()
check(#ws.text == 2, "the same pixel is not sent again")
editor.spritePos = { x = 5, y = 4 }
hover_timer().ontick()
check(#ws.text == 3 and ws.text[3]:find('"x":5', 1, true), "a pixel moved to is sent")
editor.spritePos = { x = 300, y = 4 }
hover_timer().ontick()
editor.spritePos = { x = -1, y = 4 }
hover_timer().ontick()
check(#ws.text == 3, "a pixel outside the canvas is not sent")
editor.spritePos = { x = 6, y = 7 }
prefs.hover = false
hover_timer().ontick()
check(#ws.text == 3, "with hover off nothing is sent")
prefs.hover = true
hover_timer().ontick()
check(#ws.text == 4 and ws.text[4]:find('"x":6,"y":7', 1, true), "...and with it back on, the pixel the cursor is on")
check(commands.RoodLiveHover ~= nil, "a command switches hover")
local plain = sprite_of("not_a_rood_file")
plain.layers = { { name = "Layer 1", isGroup = false } }
editor.sprite = plain
editor.spritePos = { x = 9, y = 9 }
hover_timer().ontick()
check(#ws.text == 4, "a sprite that is not a rood package's sends nothing")
editor.sprite = sprite

-- the studio's pick
ws.onreceive(WebSocketMessageType.TEXT, '{"select":{"material":"m0123456789_p10_c3b70","x":7,"y":9,"scroll":false}}')
local r = selection.selected
check(r and r.x == 7 and r.y == 9 and r.width == 1 and r.height == 1, "a texel picked in the studio is selected as one pixel")
check(editor.scroll.x == 0 and editor.scroll.y == 0, "a hover pick does not scroll the view")
ws.onreceive(WebSocketMessageType.TEXT, '{"select":{"material":"m0123456789_p10_c3b70","x":20,"y":30,"scroll":true}}')
r = selection.selected
check(r.x == 20 and r.y == 30 and editor.scroll.x == 20.5 and editor.scroll.y == 30.5, "a click pick selects the texel and scrolls the view to its centre")
ws.onreceive(WebSocketMessageType.TEXT, '{"select":{"material":"m0123456789_p11_c0000","x":1,"y":2,"scroll":true}}')
check(app.sprite == other and selection.selected.x == 1, "a texel of another open sprite makes it the active one")
ws.onreceive(WebSocketMessageType.TEXT, '{"select":{"material":"nothing_like_it","x":1,"y":2,"scroll":false}}')
check(selection.selected.x == 1, "a texel of a sprite that is not open selects nothing")
ws.onreceive(WebSocketMessageType.TEXT, '{"ok":true,"game":true,"locate":true,"note":"","triangles":2}')
ws.onreceive(WebSocketMessageType.TEXT, '{"ok":true,"game":true,"note":"","ms":3.5}')

print(failures == 0 and "PASS: extension" or ("FAIL: extension (" .. failures .. ")"))
os.exit(failures == 0 and 0 or 1)
