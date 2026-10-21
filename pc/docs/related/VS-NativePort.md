# VS-NativePort (JEschete/VS-NativePort)

Notes on another native port of *Vagrant Story*, read on 2026-10-21 at the
head commit "Align audio plan with sfizz decision". This page is for picking
up ideas; it is not a review of their work.

**Licence: none.** The repository has no LICENSE file. Nothing from it
(code, text, tables, ledgers, data) may be copied into rood-boi. Everything
below is a description in our own words of what they do; any idea we take is
reimplemented from the decompilation and our own observations. Their ledgers
are cited by path and row id only so that a reader can look a question up,
not as a source of names to paste.

## What it is

- **Target and toolchain.** Windows x64 only, one Visual Studio solution,
  clang-cl, C++23 (copied C is allowed while it is being promoted). SDL3 for
  platform, input and GPU presentation. Python tooling for ledgers, copying
  and lint. A C# companion app (VS-Workshop) serves as a decoder oracle for
  on-disc formats.
- **How they treat the original code.** Not emulation and not a recompiler.
  They keep a Windows copy of the rood-reverse decompilation under
  `resources/` as a read-only reference, and a generator
  (`tools/port/copy_to_native.py`, `copy_fixups.py`, `copy_manifest.json`)
  copies every function once into `Native/` under new system/namespace names
  (Engine, Content, Game, Desktop). Byte matching is explicitly not a goal
  (decision D3 in `Docs/NATIVE_SOURCE_PORT_PLAN.md`); the end state is
  rewritten C++ with no guest RAM, no GPU packets, no SPU registers. Today the
  runtime still has compatibility shims for all three. Functions graduate to
  hand-owned files (`tools/port/owned.csv`); the generator refuses to run if a
  generated file was edited.
- **Renderer.** A CPU rasteriser over a 1024x512 16-bit page image
  ("PageSpace", decision D21) executes the game's GPU packets with the
  original texture-page, CLUT, mask and blend rules; SDL3 GPU only presents
  the result. A typed draw queue with sort keys is planned to replace the
  packet path (`Docs/EMULATION_COMPATIBILITY_AUDIT.md`, EC-01). Widescreen,
  internal resolution and 60 fps are Phase 2 ideas (`Docs/PhaseIdeas/Phase2.md`),
  not started.
- **Assets.** Plan to convert every disc file up front (`Docs/ASSET_PIPELINE.md`):
  JSON catalogs for executable tables, glTF for models and rooms, PNG,
  WAV/SFZ instruments, AKAO sequences as event JSON, XA to FLAC, movies to
  AV1. Partly done (the intro movie runs from a converted file).
- **How they verify behaviour.** No byte-level oracle against the PS1 code.
  Instead: Catch2 unit/regression tests per fix (about 300 cases), scripted
  input replays (`--press tick:Button`, `--scripted`) with captures,
  Debug-vs-Release comparison of per-tick state snapshots and RNG draw logs,
  occasional side-by-side screenshots against DuckStation, and an owner
  playtest. Every claim is written up as "acceptance evidence" with its exact
  inputs, run ids and limits (see below).
- **Status.** Boots, title, intro movie, New Game, the prologue is playable
  end to end by the owner's report, menus mostly restored, prologue combat
  works; sound outside the opening movie is the next item. Many assembly-only
  functions are still stubs that log when reached.
- **Activity.** Very active: the repository history includes the whole
  upstream decomp history, but their own work starts in early October 2026
  and has been roughly 180 commits since, daily, mostly by one author with
  agent help. Expect the details below to go stale within weeks.

### What they say about rood-boi

They know us well and treat us as a CC0 donor:

- `Docs/TOOLS_INVENTORY.md` lists rood-boi as a "VENDOR" reference: same
  decomp, reaches gameplay, uses a guest-RAM model (which they reject), but
  its device code is fair to lift (CD drive, XA, software MDEC, software SPU,
  hardware GPU backend, memory card, gamepad, overlay statics), and our
  `pc/docs/LP64_Audit.md` and `Platform_Support.md` gave them their 64-bit
  hazard counts.
- `Docs/NATIVE_SOURCE_PORT_PLAN.md` (decision D15 and the licensing section)
  allows lifting CC0 rood-boi code with attribution; GPL and unlicensed code
  is reference only.
- `THIRD_PARTY.md` credits rood-boi for the C their battle AI, navigation,
  terrain-height, engagement and actor-cleanup files were adapted from
  (`pc/src/battle_helpers.c`, `battle_movement.c`, `battle_gte.c`,
  `actor_cleanup.c`, `actor_update.c`), pinned to one of our revisions.
  Several `Native/Game/src/Battle/Ai/*.cpp` headers name our files and lines.
- `Docs/ASSEMBLY_GAPS.md` and `tools/port/audit_assembly_gaps.py` scan
  rood-boi as one of four references and note that many C counterparts for
  assembly-only functions exist only in our `pc/src`. `Docs/HANDOFF.md` tells
  their agents to check our C before writing a function from assembly.

So the flow of code is one way (us to them), which is fine under CC0. Our
port-written C for the 184 battle assembly functions is the piece they value
most.

## Ideas worth borrowing

Ranked by value to rood-boi now.

| # | Topic | What they do | Where (their repo) | rood-boi today | Suggestion |
|---|---|---|---|---|---|
| 1 | Encounter lab | A separate executable links the real game libraries and opens an encounter builder: pick zone, room, enemy definition(s) from that zone, count, tile offsets and spacing, Ashley's tile and elevation, RNG seed, and an "Ashley profile" (New Game kit, post-prologue menu gates, a post-Minotaur set of chain/defence abilities, spells and a Break Art). A floor preflight rejects starts on pits, blocked or partial tiles with a reason, never nudging positions. Presets save to JSON and replay with `--scenario --start`. F6 pause, F7 single-step, F10 back to the builder. | `Docs/COMBAT_LAB.md`, `Native/Tools/CombatLab/`; commits "Add standalone Combat Lab with persistent encounter traces", "Reject unsupported Combat Lab starting tiles", "Complete native ground combat and Lab action profiles and effects" | Room Browser warps with or without a room's own enemies (`pc/src/room_warp.c`, RPC `room_goto`), `pc/tests/enemy_soak.py` walks rooms with their enemies, checkpoint saves (`pc/docs/Testing.md`). No way to choose which enemies, how many, where, or Ashley's ability set. | Add an `encounter` form of `room_goto` (enemy records from the zone's ZND, tiles, seed) plus a few Ashley fixtures (abilities/spells unlocked) as card fixtures, all through RPC so `rood_rpc.py` and the editor can drive it. This is the fastest way to exercise the battle code we are porting (STATE/MEANINGS work, Zig ports) on demand rather than by finding the right room. |
| 2 | Per-tick structured trace with RNG draws | Every lab run writes a directory with a manifest (source revision, dirty tree, options), the encounter definitions, and a JSONL stream of per-tick snapshots (HP/MP/risk/status, actor commands, positions, action/target state, AI candidates and destinations, pending loot) plus every RNG draw with its seed. Two builds that agree on every snapshot and every RNG record are taken as behaving the same. | `Docs/COMBAT_LAB.md` ("Extensive diagnostics", "Real action and visual evidence"); `Native/Tools/CombatLab/src/Trace.cpp` | The Oracle's page digests and mark/rewind (`pc/docs/Oracle.md`) and `snapshot_diff.py` compare memory pages in one build. 32-bit and 64-bit builds cannot be compared page for page because layouts differ (`pc/docs/LP64_Audit.md`). | Add a semantic trace: per-vsync battle snapshot from `vs_battle_actors`/`D_800F19CC` fields we already name in `pc/docs/porting/battle/MEANINGS.tsv`, plus a log of `vs_main_getRand*` calls (caller, value, `randIndex`). Diffing this between 32-bit and 64-bit, or between C and a Zig port of a battle unit, is the layout-independent parity check the porting plan needs. |
| 3 | Battle field meanings (static naming pass) | Every global, type, field and function of the decomp has a ledger row with a proposed name, purpose and a confidence, evidence-checked against source lines. A trace queue lists what static reading could not settle, with breakpoint and watch addresses. | `Docs/ledgers/DATA_LEDGER.csv`, `FIELD_LEDGER.csv`, `TYPE_LEDGER.csv`, `FUNCTION_CATALOG.csv`, `TRACE_QUEUE.csv`; `Docs/NAMING_CONVENTIONS.md` §11; `Docs/RUNTIME_TRACING.md` | `pc/docs/porting/battle/STATE.tsv` has 701 rows; only about 107 have a hand-written meaning in `MEANINGS.tsv`. Of the 594 without one, about 334 have a high-confidence purpose in their DATA_LEDGER. | Use their ledgers as a lookup of *where to look*, then confirm each meaning from our own reading or a trace and write it in our words. Pointers for the rows below (their ledger id after the arrow). |
| 4 | Graphical regressions as tests, found by real routes | Each visual bug becomes a synthetic test that fails before the fix: rasteriser top-left coverage for scaled GIM atlas tiles (skyline seams in the Bardorba bedroom shot), viewport bounds with poisoned surrounding page memory at both frame-buffer origins, the Game Over negative-fade tile's link. Then a scripted route to the exact scene and a capture, optionally compared against DuckStation with an aligned crop and a mean RGB difference. | `Native/Tests/src/RasterBoundsTests.cpp`, `ViewportBoundsTests.cpp`, `DataMenuTests.cpp`; `Docs/HANDOFF.md` sections on viewport boundaries and the bedroom window atlas seams; commits "Correct top-left raster coverage for scaled atlas tiles", "test: guard viewport bounds and record render verification", "Fix Game Over fade packet links" | Many smokes per feature (`pc/tests/*_smoke.py`), `classic_parity.py` packet-for-packet for the classic HUD, `title_fade_smoke.py`. The scaled GIM tile draw (`func_800CBBCC`, `5BF94.c`) goes through `gpu_hw.c` anchoring; our Game Over and its fade are only exercised by the soaks; the recent Continue-flicker fix ("Game Over > Continue: the LOAD menu clears both frame buffers first") has no dedicated smoke. | Add two route smokes: (a) the Bardorba bedroom window shot (ZONE018/MAP062, `ENKEI2.GIM`) at 1x and 4x internal resolution and at 16:9, checking the skyline GIM quads for seams between atlas tiles; (b) Game Over at 4:3 and 16:9: the full-screen negative fade (MENU7 `_displayGameOverScreen`, 320x240 semi-transparent tile) must cover the widened view and darken the world evenly, then Continue and Title both land clean. |
| 5 | Acceptance evidence discipline | Every fix records the exact input script and ticks, run ids for Debug and Release, test counts, what was verified and, explicitly, what it does *not* establish. Issues stay open until the wider acceptance is shown. Rejected experiments are kept with their numbers so they are not retried. | `Docs/HANDOFF.md`, `Docs/COMBAT_LAB.md`; commits "Record issue 6 acceptance evidence", "Document Game Over fade verification", "Record Bat flight verification" | Commit messages and status notes say what changed; `pc/docs/Unimplemented_Paths.md` lists traps; tests are the evidence. The "does not establish" habit is informal. | When a doc or merge claims a visual or battle fix, add one line "Not shown by this:" (other rooms, other enemies, 32-bit, GL). Keep a short "tried and rejected" list in the relevant `pc/docs/*.md` (e.g. frame pacing experiments) so agents do not repeat them. |
| 6 | Module reload and "second time" bugs | They list which overlay statics must be reset on reload and treat any "breaks the second time a screen opens" bug as a reset problem first. They also had a Game Over event VM state leaking into the next encounter and fixed it by resetting event playback/thread state on session start. | `Docs/PORTING_WATCHLIST.md` §1; commit "Repair shared effect request aliases and event session reset" | `pc/src/overlay_statics.c` restores overlay statics on load (we already do this). Room Browser warps and fast travel re-enter gameplay without a fresh load. | Add a check to the Room Browser and soak harness: after Game Over > Continue, and after a warp out of a room with a running event, the event VM (`_evtFile` and thread state) is idle. Cheap to assert through RPC. |
| 7 | Audio plan: SFZ instruments driven by the game's sequencer | Decision D23: extract each instrument bank as SFZ text plus WAV samples, play them with the BSD-licensed sfizz sampler, keep the AKAO sequencer as game logic and drive sfizz with note/CC/pitch-bend events; reverb is a mapped preset; optional PS1-style interpolation for comparison. Not implemented yet; the risk they list is matching PS1 ADSR curves. | `Docs/PORTING_WATCHLIST.md` §4, `Docs/ASSET_PIPELINE.md` §6.5, `Docs/EMULATION_COMPATIBILITY_AUDIT.md` EC-02; commit "Align audio plan with sfizz decision" | A complete software SPU fed by the game's own driver (`pc/src/audio_spu.cpp`, `audio_libspu.c`, `pc/README.md` audio section): exact envelopes, reverb, IRQ-driven streaming. Works now. | Do not replace the SPU. The reusable part is the export: an optional tool that writes the game's instrument banks as SFZ+WAV (and sequences as MIDI) would give modders and the plugin system a standard way to audition and replace instruments, with our SPU staying the reference. Low priority. |
| 8 | Assembly-gap census across references | A script lists every function that is assembly-only in the decomp, then looks for a C counterpart in each reference tree (upstream, a fork, rood-boi), records revisions and fingerprints, and reports what is still stubbed. | `Docs/ASSEMBLY_GAPS.md`, `Docs/ASSEMBLY_GAP_COVERAGE.csv`, `tools/port/audit_assembly_gaps.py` | `pc/docs/porting/battle/FUNCTIONS.tsv` marks D2 rows and where their PC C lives; `pc/tools/porting_tables.py` builds it. | Nothing to take for battle. If we extend the tables beyond battle (MENU, effects), a column "C counterpart: where" like theirs is worth keeping. |
| 9 | Source quality baseline | clang-format/cpplint/clang-tidy/ruff/jscpd run on changed code; existing findings live in a checked-in baseline and only *new* findings fail. | `Docs/QUALITY.md`, `quality-native.cmd`, `tools/lint/` | `make commit-check`, `pc/tools/engine_boundary.py`, `lp64_alias_scan.py`, `prompt_privacy.py`. | Optional: a "no new findings" clang-tidy pass on `pc/src` changed files, with a baseline file, would catch 64-bit and aliasing slips earlier. Only if cheap to run on Linux. |
| 10 | Docs organisation | One plan with a decision log (D1..D23, who decided, date), a watchlist of porting hazards with how each is handled, an audit of remaining emulation-like code with retirement gates, a single HANDOFF file of current checkpoints. | `Docs/NATIVE_SOURCE_PORT_PLAN.md` §0, `Docs/PORTING_WATCHLIST.md`, `Docs/EMULATION_COMPATIBILITY_AUDIT.md`, `Docs/HANDOFF.md` | Many focused design docs (`pc/docs/*.md`), dated plans (`pc/docs/plans/`), memory notes. Decisions are spread across plans. | A short `pc/docs/Decisions.md` (one line per standing decision, date, link to the plan) would help agents find the rules (e.g. classic UI respected, tunable not hard-coded, SDL3/64-bit/Vulkan order) without reading every plan. |

### Battle meanings to look up (pointers only)

Symbols in our `STATE.tsv` with no row in `MEANINGS.tsv` for which their
`Docs/ledgers/DATA_LEDGER.csv` has a high-confidence entry. Topics are our
paraphrase; confirm each from our own reading before writing a meaning.

| Our symbol | Their ledger id | Topic |
|---|---|---|
| `D_800F5900` | G00594 | AI planning workspace (reaction record, candidates, exclusions; size given) |
| `D_800E81C4` | G00022 | which action categories aim at body parts rather than whole actors |
| `D_800F49F0`, `D_800F4B08`, `D_800F49F9` | G00428, G00433, G00431 | terrain/ledge collision probe: tile, world position, direction |
| `D_800F49F8` | G00430 | gimmick an actor is standing on |
| `D_800F4CB0`, `D_800F4CB1` | G00488, G00489 | entry counts of the two target-selection columns |
| `D_800F1DC4`, `D_800F1DC0`, `D_800F1DBC`, `D_80068EB4` | G00351, G00349, G00347, G00605 | room weather: intensity, particle count, ambient sound, volume table |
| `D_800F1B98`, `D_800F1ABC`, `D_800F1A78` | G00305, G00301, G00293 | full-screen frame-buffer effect: state, packet block, tint (relevant to fades and screen effects at 16:9) |
| `D_800F19A8` | G00272 | texture-page reservation/ownership table |
| `D_800E8F30`, `D_800E8F90` | G00069, G00070 | actor texture slot origins and row offsets in VRAM |
| `D_800F1BA8` | G00309 | room palettes (three plus the gimmick palette) |
| `D_800F55F4` | G00553 | particle free list head (see watch list) |
| `D_800F54D8`, `D_800F55A8`, `D_800F5520` | G00546, G00550, G00548 | effect camera, saved action camera, effect camera motion endpoints |
| `D_800E8F2C`, `D_800F4528` | G00067, G00404 | shared animation sequence banks: active index and data |
| `D_800F4BE2`, `D_800F4BE0`, `D_800F4C24` | G00455, G00454, G00465 | event-driven room change, event source, chained event buffer |
| `vs_main_statusEffectParams` | G01906 | the 32 status effects' parameters |

Fields: `Docs/ledgers/FIELD_LEDGER.csv` rows `T00333.*` describe the bits of
the MPD floor-cell word (`_mpdRoomSection3`: surface kind, pit, event and
room triggers, door passability, a bit that makes the cell solid for the
target sphere, camera occlusion), which our terrain queries and the
Room Browser floor preview use without names. Their `TYPE_LEDGER.csv` has
about 160 rows under the AI namespace (tile words, candidate and choice
views, route gates, arena orbit state) keyed by decomp file and line.
`Docs/RUNTIME_TRACING.md` lists the layout questions they could not settle
statically (e.g. a camera-shake read two bytes past its declared field, a
vibration pattern element that probably starts 8 bytes later); these are
good candidates for our Oracle, which can answer them by watching memory.

## Where we're ahead

- **Rendering.** Native meshes on Vulkan/GL with real depth, up to 8x
  resolution, widescreen with screen-furniture handling, frame interpolation,
  RT shadows, path-traced/probe GI, volumetrics. They present a CPU-rasterised
  320x240 page image; widescreen and 60 fps are still ideas.
- **Behavioural oracle.** Byte-matching PS1 build of the same sources, page
  digests, mark/rewind, record/replay by vsync, alternate-owner comparison
  (`pc/docs/Oracle.md`). They have no equivalent of comparing against the
  original code in-process.
- **Audio.** Working software SPU with the game's own driver; they have sound
  only in the intro movie.
- **Platform.** Linux and Windows, 32-bit and 64-bit builds, headless EGL
  tests, an RPC that drives everything.
- **Tooling.** Live editor, Room Browser, fast travel, modern menus, Lua
  plugins, dev-tools panel for every tunable.

## Watch list

Re-check these on a later read of their repo.

- **Particle pool exhaustion.** They found that the particle emitter
  dereferences the free-list head when the pool is full (their F01515 is our
  `func_800D2ADC`; the allocator is `func_800D27F0` in
  `src/BATTLE/BATTLE.PRG/5BF94.c`, whose error call `func_800CE644` is a
  no-op, so a NULL node is used). On the PS1 that reads address 0. Check what
  our 32- and 64-bit builds do with many simultaneous particle effects.
- **Eight Minotaur Zombies.** They see the frame's primitive cursor corrupted
  before room drawing with eight large enemies in one room. Worth an
  `enemy_soak.py`-style run with the same roster to see if we overflow a
  primitive buffer too.
- **Analog stick byte order.** They note the controller sends the right stick
  before the left, so the decomp's names may be swapped
  (`Docs/PORTING_WATCHLIST.md` §9). Check against our camera-stick handling.
- **Image assets with a header or palette first.** The NOW LOADING image is a
  size header followed by pixels, and the load screen's CAUTION label sits
  after its palette ("Loading screen image: rgb555Sized assets split into
  header + pixels from the splat config"). Irrelevant while we build from the
  decomp's own asset rules; matters if we move assets to data files.
- **Their Combat Lab trace browser and coverage report**, listed as future
  work. If they build a coverage matrix of actions/spells/arts per enemy,
  the idea (not the data) is useful for our soaks.
- **Their typed render queue (EC-01) and SFZ audio (D23)** once implemented:
  look at how they map palette animation and framebuffer readback effects to
  modern handles, and how close SFZ envelopes get to PS1 ADSR.
- **Their Phase 2 list** (`Docs/PhaseIdeas/Phase2.md`): widescreen without
  revealing hidden areas, chain-prompt readability, targeting-sphere
  precision. Compare with our modern-menus and HUD plans when they start.
- **Licence.** If they add a licence later, re-read what it permits; until
  then, ideas only.
