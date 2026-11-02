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
- `source/StoatworksAbout*.h` and `ATTRIBUTIONS.md` are provisional hand
  copies until the repo is registered in stoatworks-backend; after that they
  are generated there (`sync-about.py`, `sync-attributions.py`). Edit the
  sources in the backend then.

## Not done yet
- Never loaded into Resolume on any platform. No Windows build.
- Public at github.com/stoatworks-labs/bluebook, with no CI yet. Not registered
  in stoatworks-backend: no website entry. No user guide, video or presets.
