return {
  id = "rood.photo",
  name = "Photo mode",
  version = "2.0.0",
  api_version = 1,
  description = "Pauses the game in the field or in battle (the music plays on), hides the HUD and gives you a free "
    .. "camera (orbit or free), a tabbed card (Camera, Lens, Effects, Colour, Frame, Presets) and PNG photos. "
    .. "Bindings > Photo mode (F12; on a pad Back + D-pad up). Everything is given back when you leave.",
  author = "rood-boi",
  capabilities = { "game.pause", "camera", "post", "ui.hide", "capture", "input.capture" },
}
