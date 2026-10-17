local t = rood.test

t.case("another plugin's globals are invisible", function()
  t.eq(b_secret, nil)
  t.eq(secret, "only in a")
end)
