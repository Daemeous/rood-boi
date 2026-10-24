# Window aperture live-scene review (2026-10-23)

This is **not** a passing floor-band demonstration. Generated pane mappings are disc-derived; a live aperture only emits when its palette-qualified texture use and scoped room polygon are present. `pattern=0,pattern_mode=1` is the uniform-aperture control; `pattern=1,pattern_mode=1` samples the curated emissive mask. Legacy `pattern_mode=0` changes the angular model as well, so it is not the pattern A/B baseline.

On the 64-bit Vulkan build (`--nrd off`), the scratch-card visual smoke ran with a private lighting/user layer and RPC port, 1280×960 output, 60 settle presents, 90 measured presents ×3 per mode, under the heavy lock. Results are in ignored `pc/build/window-pattern-check-{38full,other,69c}/` (reports and screenshots). GPU values below are medians of the three `perf.gpu_ms` readings (paused-present wall time is **not** GPU time):

| Room | Live matched uses / affected surfaces | Pattern 0 → 1 changed pixels (>3/255) | GPU ms pattern 0 → 1 | Interpretation |
| --- | --- | ---: | ---: | --- |
| E008 37/0 | E008 2 uses / 4 surfaces; E014 2 / 2 | 56 / 1,228,800 | 0.635 → 0.656 | Changes small at the save-card game camera. |
| E008 38/0 | **E008 0 / 0**; E014 2 / 2 | 105,518 / 1,228,800 | 0.738 → 0.718 | Large loss of uniform light, **not** proof of E008 bars. |
| E008 69/0 | **E008 0 / 0**; E014 2 / 2 | 19,913 / 1,228,800 | 0.653 → 0.661 | Pattern change is not evidence of E008 transmission. |
| E017 28/14 | E017 2 / 18 (some surfaces not allowed by scope) | 183 / 1,228,800 | 1.051 → 1.197 | Limited visible change with a 1×1 matched texture. |

The 38/0 warp **does arrive** (`state.zone=38,room=0`, camera off), as does 69/0 once given an explicit centre floor `cell` (69/0 has no door). Before that change, `room_goto` returned a refusal and the old test only reported “warp FAIL”. Renderer cache is invalidated by room, emissive map serial and catalog generation; A/B calls reload and wait for presents. E008 is absent from the live texture-use list in 38/0 and 69/0, even though the generated light records include its faces. Offline 38/0 polygons use tpage 20, clut 14450, e.g. the 17×31 E008 box [0,128]–[16,158]; the live use list on the same page/palette has other boxes, e.g. [32,128]–[40,158] and [96,128]–[126,160], but no E008 key. Determine why captured live UV/texture-use bounds differ from the disc-derived map before interpreting these rooms as a rendering regression or changing the match criteria. Do **not** substitute another palette/key or expand the shipped mask to make a picture.

For 38/0, old proposed floor crops (dark 520,800,30,30; bright 640,840,30,30) have substantial uniform-vs-off signal but approximately **zero** patterned-vs-off signal in *both* crops. A scan of all 30×30 crops below y=600 with uniform signal >2/255 found 125 candidates, none retaining over 0.002 of the uniform signal. There is no valid bright-pane ROI for the claimed E008 band contrast in this capture; the earlier `--dark-roi ... --bright-roi ...` assertion remains FAIL and must not be changed to pass by relabelling two dark crops. At 28/14, even the strongest 10×10 candidate has only 2.04/255 uniform signal and 0.87 retained, while no 30×30 floor crop has >2/255 signal. Thus none of these default-camera captures supports a robust paired bright/dark projected-floor assertion. A new reviewed camera/receiver and resolved live E008 matches are prerequisites.

E008's **shipped** mask is two rectangles `[8,11,13,12]`, `[18,1,26,6]` intersected with palette indices 14/15. On its matched textures only ~2–5 texels survive per texture; large bright bars are not an honest expectation. Keep the mask and authored identities unchanged. The generator continues to exclude fully masked panes and derive colours from surviving texels, while pattern faces retain full pane UV geometry. Regenerate the three affected room files when the generator and checked-in data disagree; no new texture matches or mask regions are introduced.

Reproduce with an existing canonical `pc/build/test-card1.mcd` fixture, a local disc and build, and unique scratch port/output:

```sh
flock "$ROOD_BUILD_LOCK" nice -n 10 python3 pc/tools/build_boot.py --arch 64 --nrd off
flock "$ROOD_HEAVY_LOCK" nice -n 10 env ROOD_ARCH=64 ROOD_RENDERER=vulkan ROOD_RPC=tcp:49387 python3 pc/tests/window_pattern_smoke.py --rooms 38:0 --out pc/build/window-pattern-check-38full --settle 60 --frames 90 --repeats 3
```

Run the other rooms with different output/ports. The report now records game state, camera and live emissive uses/surfaces alongside the PNGs and timings, to distinguish a real room transition and actual sources from merely generated light IDs. `--dark-roi` and `--bright-roi` remain opt-in until a valid receiver/camera is established.
