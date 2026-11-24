-- Rood live (CC0). Sends the strokes painted in the channel layers ("Height", "Metal", "Roughness"...: the bridge's greeting names them, from
-- pc/tools/aseprite/channels.json) and the preset layer ("Material": painted with the palette's
-- preset swatches, one stroke sets every channel the preset has) of a sprite made by
-- `paint_roundtrip.py export ... --painter aseprite` to `paint_roundtrip.py live <package>` over a WebSocket on localhost,
-- which puts the map into the running game. Needs Aseprite 1.3 or later (sprite.events, WebSocket, Image.bytes, Timer),
-- the sprite in RGB colour mode, and the permission Aseprite asks for the first time (network access: give it).
-- Message to the bridge (one binary frame): u32 big-endian header length, a JSON header, the cel's RGBA bytes.
-- Replies are JSON text: {"ok":..,"game":..,"note":".."}.
--
-- The texel locator (the model studio, Gitea #37): Aseprite has no mouse-move event, but app.editor.spritePos is the pixel under the
-- cursor, so a timer reads it (HOVER_INTERVAL) and, when it changes over a rood sprite, sends {"locate":{"material","x","y"}} as text:
-- the game's studio marks that texel on the model. The other way the bridge sends {"select":{"material","x","y","scroll"}} (the
-- texel the cursor or a click picked on the model in the studio): the texel is selected (a 1 x 1 selection) and, for a click, the
-- view scrolls to it (app.editor.scroll). Preference "hover" (Rood live: locate hover on/off) switches the first off.

local DEFAULT_PORT = 47480
local HOVER_INTERVAL = 0.05 -- seconds between looks at the cursor's pixel
local SEND_INTERVAL = 0.1 -- seconds: about 10 updates a second; the last change of a stroke goes out on the next tick
local COLOUR_LAYER = "Game colour (reference)"
local CHANNELS = {}           -- layer name -> true, filled from the bridge's greeting (the channel table is the bridge's)
local PRESET_LAYER = "Material" -- (the greeting says it)

local function flag(name) return WebSocketMessageType and WebSocketMessageType[name] or -1 end

function init(plugin)
  local prefs = plugin.preferences
  prefs.port = prefs.port or DEFAULT_PORT
  if prefs.hover == nil then prefs.hover = true end

  local ws, timer, dialog, hoverTimer
  local connected, lastSpot = false, nil
  local status = { text = "not connected" }
  local flush, ensureTimer
  local presets = {} -- from the bridge's greeting: { name, rgb = "rrggbb", about = "metal 1, roughness 0.12" }
  local dirty = {}    -- layer name -> sprite
  local attached = {} -- sprites whose events are hooked

  local function show(text)
    status.text = text
    if dialog then pcall(function() dialog:modify{ id = "status", text = text } end) end
  end

  local function swatchText()
    local ok, text = pcall(function()
      local c = app.fgColor
      local hex = string.format("%02x%02x%02x", c.red, c.green, c.blue)
      for _, p in ipairs(presets) do
        if p.rgb == hex then
          return string.format("%s: %s", p.name, p.about ~= "" and p.about or "-")
        end
      end
      return "(not a preset swatch)"
    end)
    return ok and text or ""
  end

  local function showSwatch()
    if dialog then pcall(function() dialog:modify{ id = "swatch", text = swatchText() } end) end
  end

  local openDialog

  local function pickPreset(p)
    pcall(function()
      app.fgColor = Color{ r = tonumber(p.rgb:sub(1, 2), 16), g = tonumber(p.rgb:sub(3, 4), 16), b = tonumber(p.rgb:sub(5, 6), 16) }
      local sprite = app.sprite
      if sprite then -- (to the Material layer: the swatch is painted there)
        for _, layer in ipairs(sprite.layers) do
          if layer.name == PRESET_LAYER and not layer.isGroup then app.layer = layer end
        end
      end
    end)
    showSwatch()
  end

  local function readPresets(message)
    local list = {}
    for name, rgb, rest in message:gmatch('{"name":%s*"([^"]*)",%s*"colour":%s*"#(%x%x%x%x%x%x)"([^}]*)}') do
      list[#list + 1] = { name = name, rgb = rgb:lower(), about = rest:match('"about":%s*"([^"]*)"') or "" }
    end
    if #list == 0 then return end
    local same = #list == #presets
    for i, p in ipairs(list) do
      if same and (presets[i].name ~= p.name or presets[i].rgb ~= p.rgb) then same = false end
    end
    presets = list
    if same then
      showSwatch()
    elseif dialog then -- (the window is rebuilt: its buttons are the presets)
      pcall(function() dialog:close() end)
      dialog = nil
      openDialog()
    end
  end

  -- (the status window is only a convenience: never let it stop the painting)
  openDialog = function()
    if dialog then return end
    pcall(function()
      local d = Dialog{ title = "Rood live", onclose = function() if dialog == d then dialog = nil end end }
      d:label{ id = "status", label = "Game", text = status.text }
      d:label{ id = "swatch", label = "Swatch", text = swatchText() }
      for i, p in ipairs(presets) do -- one button per preset: sets the colour, picks the Material layer
        d:button{ text = p.name, onclick = function() pickPreset(p) end }
        if i % 3 == 0 then d:newrow() end
      end
      d:show{ wait = false, bounds = Rectangle(20, 60, 400, 120 + 32 * math.ceil(#presets / 3)) }
      dialog = d
    end)
  end

  -- the layers the bridge paints: {"layers":[{"layer":"Height","channel":"height"},...],"preset_layer":"Material"}
  local function readLayers(message)
    local found = {}
    for layer in message:gmatch('{"layer":%s*"([^"]*)",%s*"channel"') do found[layer] = true end
    local preset = message:match('"preset_layer":%s*"([^"]*)"')
    if preset then found[preset], PRESET_LAYER = true, preset end
    if next(found) == nil then return end
    CHANNELS = found
    for _, sprite in ipairs(attached) do -- (the bridge learns every layer)
      for name in pairs(CHANNELS) do dirty[name] = sprite end
    end
    ensureTimer()
  end

  local function findSprite(title)
    local list = {}
    pcall(function() for _, sprite in ipairs(app.sprites) do list[#list + 1] = sprite end end)
    for _, sprite in ipairs(attached) do list[#list + 1] = sprite end
    for _, sprite in ipairs(list) do
      local ok, found = pcall(function() return app.fs.fileTitle(sprite.filename) == title end)
      if ok and found then return sprite end
    end
  end

  -- {"select":{"material":"<title>","x":3,"y":4,"scroll":true}}: the texel the studio picked on the model
  local function selectTexel(message)
    local title = message:match('"material":%s*"([^"]*)"')
    local x, y = tonumber(message:match('"x":%s*(%-?%d+)')), tonumber(message:match('"y":%s*(%-?%d+)'))
    if not title or not x or not y then return end
    local sprite = findSprite(title)
    if not sprite then show("studio picked a texel of " .. title .. " (not open)"); return end
    pcall(function() if app.sprite ~= sprite then app.sprite = sprite end end)
    local ok = pcall(function() sprite.selection:select(Rectangle(x, y, 1, 1)) end)
    if message:find('"scroll":%s*true') then
      pcall(function()
        local editor = app.editor
        if editor and editor.sprite == sprite then editor.scroll = { x = x + 0.5, y = y + 0.5 } end
      end)
    end
    pcall(function() app.refresh() end)
    if ok then show(string.format("studio: texel %d, %d", x, y)) end
  end

  local function onReceive(messageType, message)
    if messageType == flag("TEXT") and message:sub(1, 10) == '{"select":' then
      selectTexel(message)
      return
    end
    if messageType == flag("OPEN") then
      connected = true
      show("connected; waiting for the game")
      ws:sendText("hello") -- (its reply names the layers; every layer of an attached sprite is sent then)
    elseif messageType == flag("CLOSE") then
      connected = false
      show("bridge not running (paint_roundtrip.py live <package>)")
    elseif messageType == flag("TEXT") then
      local ok = message:match('"ok":%s*(%a+)') == "true"
      local game = message:match('"game":%s*(%a+)') == "true"
      local note = message:match('"note":%s*"([^"]*)"') or ""
      local ms = message:match('"ms":%s*([%d%.]+)')
      if message:find('"locate":%s*true') then -- (the answer to the cursor: the status line is for the strokes)
        if ok and game and note ~= "" then show("studio: " .. note) end
        return
      end
      if message:find('"layers"', 1, true) then readLayers(message) end
      if message:find('"presets"', 1, true) then readPresets(message) end
      if not ok then show("refused: " .. note)
      elseif game then show("live: game running" .. (ms and (" (" .. ms .. " ms)") or ""))
      else show("connected; game not running (the saved file still imports)") end
    end
  end

  local function connect()
    if ws then ws:close() end
    ws = WebSocket{ url = "ws://127.0.0.1:" .. tostring(prefs.port), deflate = false, onreceive = onReceive,
                    minreconnectwait = 1, maxreconnectwait = 5 }
    ws:connect()
  end

  local function findLayer(sprite, name)
    for _, layer in ipairs(sprite.layers) do
      if layer.name == name and not layer.isGroup then return layer end
    end
  end

  local function isRood(sprite)
    return sprite.colorMode == ColorMode.RGB and findLayer(sprite, COLOUR_LAYER) ~= nil
  end

  local function escape(s) return (s:gsub('[\\"]', "\\%0")) end

  -- The cursor over a rood sprite's canvas: its pixel, when it changed, to the bridge (the studio marks it on the model).
  local function hover()
    if not connected or prefs.hover == false then return end
    pcall(function()
      local editor = app.editor
      local sprite = editor and editor.sprite
      if not sprite or not isRood(sprite) then return end
      local pos = editor.spritePos
      if not pos then return end
      local x, y = math.floor(pos.x), math.floor(pos.y)
      if x < 0 or y < 0 or x >= sprite.width or y >= sprite.height then return end
      local title = app.fs.fileTitle(sprite.filename)
      if lastSpot and lastSpot.title == title and lastSpot.x == x and lastSpot.y == y then return end
      lastSpot = { title = title, x = x, y = y }
      ws:sendText(string.format('{"locate":{"material":"%s","x":%d,"y":%d}}', escape(title), x, y))
    end)
  end

  local function ensureHover()
    if not hoverTimer then
      hoverTimer = Timer{ interval = HOVER_INTERVAL, ontick = hover }
      hoverTimer:start()
    end
  end

  local function send(sprite, layer)
    local cel = layer:cel(1)
    local x, y, w, h, bytes = 0, 0, 0, 0, ""
    if cel then
      x, y, w, h = cel.position.x, cel.position.y, cel.image.width, cel.image.height
      bytes = cel.image.bytes
    end
    local header = string.format('{"material":"%s","layer":"%s","x":%d,"y":%d,"w":%d,"h":%d,"canvas":[%d,%d]}',
      escape(app.fs.fileTitle(sprite.filename)), escape(layer.name), x, y, w, h, sprite.width, sprite.height)
    ws:sendBinary(string.pack(">I4", #header) .. header .. bytes)
  end

  ensureTimer = function()
    if not timer then
      timer = Timer{ interval = SEND_INTERVAL, ontick = flush }
      timer:start()
    end
  end

  flush = function()
    for name, sprite in pairs(dirty) do
      dirty[name] = nil
      local ok, err = pcall(function()
        local layer = findLayer(sprite, name)
        if layer then send(sprite, layer) end
      end)
      if not ok then show("send failed: " .. tostring(err)) end
    end
  end

  local function changed(sprite)
    local layer = app.layer
    if layer and layer.sprite == sprite and CHANNELS[layer.name] then
      dirty[layer.name] = sprite
    else -- (an undo or a change from elsewhere: send both channels)
      for name in pairs(CHANNELS) do dirty[name] = sprite end
    end
    ensureTimer()
  end

  local function attach(sprite)
    if not sprite or not isRood(sprite) then return end
    for _, other in ipairs(attached) do
      local same, eq = pcall(function() return other == sprite end)
      if same and eq then return end
    end
    attached[#attached + 1] = sprite
    sprite.events:on("change", function() changed(sprite) end)
    if not ws then connect() end
    ensureHover()
    openDialog()
  end

  plugin:newCommand{ id = "RoodLiveStatus", title = "Rood live: status",
    group = "file_scripts", onclick = function() openDialog(); if not ws then connect() end end }
  plugin:newCommand{ id = "RoodLivePresets", title = "Rood live: reload presets", group = "file_scripts",
    onclick = function() if ws then ws:sendText("hello") else connect() end end }
  plugin:newCommand{ id = "RoodLiveHover", title = "Rood live: locate hover on/off", group = "file_scripts",
    onclick = function()
      prefs.hover = not prefs.hover
      lastSpot = nil
      show(prefs.hover and "locate hover on: the cursor's texel is marked in the studio" or "locate hover off")
    end }
  plugin:newCommand{ id = "RoodLivePort", title = "Rood live: port...", group = "file_scripts",
    onclick = function()
      local d = Dialog("Rood live port")
      d:number{ id = "port", label = "Port", text = tostring(prefs.port), decimals = 0 }
      d:button{ id = "ok", text = "OK", focus = true }
      d:button{ id = "cancel", text = "Cancel" }
      d:show()
      if d.data.ok then prefs.port = math.floor(d.data.port); connect() end
    end }

  app.events:on("sitechange", function() attach(app.sprite) end)
  pcall(function() app.events:on("fgcolorchange", showSwatch) end)
  attach(app.sprite)
end

function exit(plugin) end
