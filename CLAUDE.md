# bluebook

The look of a 1990s paranormal TV title sequence (step-printed footage, grainy
photo enlargements, a stretched face, a glowing hand, torchlight, a teal grade,
a caption spelling itself out) and a crime-drama freeze-frame hit, for Resolume
Arena/Avenue, as an FFGL effect. C++/GLSL, CMake MODULE → universal `.bundle`
(macOS). MIT. ID `BK01`, display name `SW Bluebook`.

Read `AGENTS.md` before touching the print stage's buffers, the 1:1 reads in
the shaders, the grain layers or the aura's streamers.

## Commands (CMake)
- Configure: `cmake -B build -DCMAKE_BUILD_TYPE=Release`
- Fast dev build: add `-DCMAKE_OSX_ARCHITECTURES=arm64`
- Build: `cmake --build build`
- Install to Resolume: `cmake --install build` (writes into Arena's Extra Effects)
- Render offline: `./build/bbtest --out /tmp/frame.png` (the defaults on the harness's card, 2 s in)
- List parameters: `./build/bbtest --list`
- Set anything by name: `./build/bbtest --set Aura=0.6 --set "Aura Colour=White" --caption "EXHIBIT 7"`
- Press something later: `./build/bbtest --at 60 Freeze=1` (let go on the next frame)
- On a still of your own: `--in still.png` (8-bit PNG, resized to `--size`)
- Film: `./build/bbtest --film 600 --size 1280x720 --script cues | ffmpeg -f rawvideo -pix_fmt rgba -s 1280x720 -r 60 -i - out.mp4`
- A clip through it: `ffmpeg -i in.mov -vf fps=60 -f rawvideo -pix_fmt rgba - | ./build/bbtest --pipe --size WxH | ffmpeg …`
- The shaders as compiled: `./build/bbtest --shaders DIR`
- Rebake the caption font: `python3 tools/make-font.py` (Pillow, numpy, scipy; output committed)

## Verify
- Everything: `tools/verify.sh` (fresh universal build, every check at 640x360
  and 320x180, again on Apple's software renderer, the negative controls, 17
  mutants, the sweep, lipo, plist, ad-hoc signature, `oxbow` if `OXBOW=` points
  at one, the bench)
- **No GL**: `--offline` (= `--names`, `--mappings` incl. the README's range
  ends, `--layout`).
- **Print**: `--identity`, `--print` (prints and dissolves frame by frame),
  `--beat`, `--weave`, `--trail`.
- **Picture**: `--enlarge`, `--grain`, `--stretch`, `--aura`, `--torch`,
  `--grade`, `--lettering` (the caption; `--caption` is the text option).
- **Freeze and the host**: `--freeze`, `--resize`, `--transparency` (`--alpha`
  is the PNG option), `--state`.
- All of them: `--all`. One raster only: `--size WxH`.
- The checks can fail: `--negative` (13 wrong models), `tools/mutate.sh` (17 mutants).
- No dead controls: `python3 tools/sweep.py` (50 swept, all live).
- Software renderer: `BBTEST_RENDERER=software ./build/bbtest --all --size 320x180`.
- Render cost: `./build/bbtest --bench`.
- Browser demo (`demo/`): `python3 demo/tools/check_shaders.py` (its shaders and
  font are the plugin's; `--write` regenerates them, never edit by hand) and
  `node demo/tools/check_port.mjs --bbtest build/bbtest` (its CPU port decides
  what the plugin does, against `bbtest --trace N`). `verify.sh` runs both.
  Deployed by `.github/workflows/deploy.yml` to bluebook-demo.stoatworks-labs.com
  (a route; see `wrangler.toml`). Change the plugin's CPU side and the port in
  `demo/plugin.js` must change with it.
- CI (`.github/workflows/ci.yml`, every push to main and every pull request):
  the build, `--list`, `--offline`, every picture check on the runner's
  software renderer at 320x180 (skipped with a warning if it has no GL),
  glslc; and the Windows build, loaded by `tools/wincheck.ps1` as a host
  would. `release.yml` builds and publishes on a `v*` tag; run by hand, it
  builds both platforms and publishes nothing.

## Notes
- **Every 1:1 read is a `texelFetch`** at `texelOf( size )`; every other read
  is `level0()`. The software renderer lands `uv` a hair off the texel centres
  and its LOD a hair above 0. Don't put a plain `texture()` back.
- **`mix( a, b, 1.0 )` is not promised to be `b`.** Freeze and both ends of
  Mix are chosen, not mixed.
- **The smear (`trail`) is RGBA32F**; everything else is RGBA16F. A one-pole in
  half floats stalls short of its target.
- **Fresh buffers are detected by size**, never by GL id: a driver hands the
  name it just freed straight back.
- All animation runs on `elapsed` (the sum of this instance's own dt), never
  the host's clock directly. The host clock's unit is voted on (rosette's).
- No trig in the GLSL: the torch's path is worked out on the CPU.
- `--list` and the offline checks are answered before a GL context is asked
  for, so CI can run them on a runner with none. Keep new GL-free checks there.
- All host parameters are 0..1 and mapped in `Controls.cpp`; options hold the
  element index; Freeze acts on the press.
- GLSL reserved words (`patch sample input output filter common active half
  layout flat packed`, `noise1`–`noise4`) must not be identifiers; `verify.sh`
  greps for them.
- Override `SetTextParameter` to return `FF_SUCCESS` for the About block, or no
  host can instantiate the plugin. Caption is a real text parameter.
- `bluebook_core` is an OBJECT library: the plugin registers itself from a
  file-scope constructor nothing references.
- The macOS build must be universal. Check with `lipo`, never the build log.
- `source/StoatworksAbout*.h` and `ATTRIBUTIONS.md` are generated in
  stoatworks-backend (`sync-about.py`, `sync-attributions.py`); edit the
  sources there, not the copies.
- The caption is ASCII: `Printable()` makes each UTF-8 character one `?` before
  anything is measured or cut. A press inside a timed hold freezes the clip
  itself, not `trail` (which holds the freeze then).

## Not done yet
- Never loaded into Resolume on macOS. On Windows, plugin-bench's Arena gate ran
  a CI build in Arena 7.27.1 on win-lab (llvmpipe): loads, registers, every
  control as declared, renders, 29 controls move Arena's picture of a still, 12
  inconclusive against the grain, none dead (README, Status).
- No preset dropdown, on purpose: Resolume saves parameter presets itself, and the
  fleet's copy-based presets fight the host's echo. The guide's Recipes are the looks.
