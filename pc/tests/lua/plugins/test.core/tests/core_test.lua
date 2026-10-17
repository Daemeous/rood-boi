local t = rood.test

t.case("version and plugin info", function()
  t.eq(type(rood.version), "string")
  t.eq(rood.api_version, 1)
  t.eq(rood.plugin.id, "test.core")
  t.ok(rood.plugin.dir:find("test.core", 1, true), rood.plugin.dir)
  t.eq(rood.plugin.elevated, false)
end)

t.case("events reach handlers as copies", function()
  local got
  local h = rood.on("test.core/ping", function(payload) got = payload end)
  local payload = { n = 1, list = { 1, 2 } }
  t.ok(rood.emit("test.core/ping", payload) >= 1)
  t.eq(got, payload)
  t.ok(got ~= payload, "a copy, not the same table")
  t.ok(rood.off(h))
  got = nil
  rood.emit("test.core/ping", payload)
  t.eq(got, nil)
end)

t.case("the core's events cannot be emitted by plugins", function()
  t.raises(function() rood.emit("tick", {}) end, "namespaced")
end)

t.case("game-time timers fire after ticks", function()
  local fired = 0
  rood.timer.after(0.05, function() fired = fired + 1 end)
  t.eq(fired, 0)
  t.wait(5)
  t.eq(fired, 1)
end)

t.case("every and cancel", function()
  local n = 0
  local id = rood.timer.every(1 / 59.94, function() n = n + 1 end)
  t.wait(3)
  t.ok(n >= 2, "fired every tick")
  t.ok(rood.timer.cancel(id))
  local before = n
  t.wait(3)
  t.eq(n, before)
end)

t.case("registry order: after, before, group", function()
  local R = rood.registry
  R.register("test.thing", "test.core/b", { v = "b" }, { after = "test.core/a" })
  R.register("test.thing", "test.core/a", { v = "a" })
  R.register("test.thing", "test.core/c", { v = "c" }, { before = "test.core/a", group = "early" })
  local ids = {}
  for _, e in ipairs(R.list("test.thing")) do ids[#ids + 1] = e.id end
  t.eq(ids, { "test.core/c", "test.core/a", "test.core/b" })
  t.eq(#R.list("test.thing", { group = "early" }), 1)
  t.eq(R.get("test.thing", "test.core/a").data.v, "a")
  t.raises(function() R.register("test.thing", "other.plugin/x", {}) end, "namespace")
  for _, id in ipairs(ids) do t.ok(R.remove("test.thing", id)) end
  t.eq(#R.list("test.thing"), 0)
end)

t.case("storage keeps plain data; files stay in their folders", function()
  rood.storage.set("answer", { n = 42 })
  t.eq(rood.storage.get("answer"), { n = 42 })
  t.raises(function() rood.storage.set("f", print) end, "function")
  t.eq(rood.storage.read("data/hello.txt"), "hello\n")
  t.raises(function() rood.storage.read("../test.sandbox/main.lua") end, "bad path")
  rood.storage.write_data("note.txt", "x")
  t.eq(rood.storage.read_data("note.txt"), "x")
end)
