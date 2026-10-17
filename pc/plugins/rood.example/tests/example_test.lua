local t = rood.test

t.case("registers its note and its setting", function()
  t.eq(rood.registry.get("rood.example.note", "rood.example/hello").data.text, "Hello from rood.example")
  t.eq(type(rood.settings.get("plugin:rood.example/greet")), "boolean")
end)
