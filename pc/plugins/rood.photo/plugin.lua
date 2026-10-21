return {
  id = "rood.photo",
  name = "Photo mode",
  version = "1.0.0",
  api_version = 1,
  description = "Pauses the game in the field or in battle, hides the HUD and gives you a free camera, a settings "
    .. "card (format, filter, vignette, grain, aberration, exposure, aperture, focus) and PNG photos. Bindings > "
    .. "Photo mode (F12; the pad's Share / Capture button). Everything is given back when you leave.",
  author = "rood-boi",
  capabilities = { "game.pause", "camera", "post", "ui.hide", "capture", "input.capture" },
}
