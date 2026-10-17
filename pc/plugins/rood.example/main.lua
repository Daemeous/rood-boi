-- An example plugin (pc/docs/Extensions.md). Off by default: turn it on in
-- the Lua console's Plugins list (F1, Window > Lua console) or with
-- RPC lua {action "enable", plugin "rood.example"}.
local greet = rood.settings.define("greet", {
  type = "boolean",
  label = "Greet in the log",
  help = "Writes a line to the log every minute of play.",
  default = true,
})

rood.timer.every(60, function()
  if rood.settings.get(greet) then
    rood.log(string.format("still here after %d s of play", math.floor(rood.clock.game())))
  end
end)

rood.registry.register("rood.example.note", "rood.example/hello", { text = "Hello from rood.example" })
