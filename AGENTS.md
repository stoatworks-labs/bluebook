# AGENTS.md: Bluebook

Invariants, decisions and traps for anyone (human or agent) changing this repo. `CLAUDE.md`
has the commands.

## The idea

The plugin is a film lab, not a copy of a title sequence. Each treatment the sequence used
is a stage, and they run in one order every frame:

1. **Print** (`Pass::Capture`, `Pass::Print`). Two held prints and a dissolve between
   them; a print is taken when the step clock's floor moves (Rate, or the host's bar phase
   for the beat modes, or BPM when the host sends no bar phase). Each print is captured
   enlarged by `1 / ( 1 - 2 weave )` and offset by up to `weave` each way, so its edge
   never reaches the frame. The result is smeared by Trail (a one-pole) into `trail`.
   The freeze reads `trail` too: a press copies what the print stage showed last frame,
   and while frozen the print stage shows that copy instead of the clip.
2. **Geometry** (`Pass::Frame`). Freeze Push about the centre and the enlargement about
   its target as one affine map, then the stretch. The enlargement's depth is in
   generations: `m = Magnify ^ ( depth / Generations )`, each generation a hair off the
   last (mis-registration in the units of the generation that made it). Magnified reads
   are a cubic B-spline (Sigg and Hadwiger), which looks like an optical print; an exact
   identity is a `texelFetch`.
3. **Aura** (`Pass::Mask`, `Pass::Blur`). The subject (Bright, Dark or Alpha key) from
   the framed picture's mip level 2, at a quarter of the raster, blurred by a separable
   Gaussian of Aura Size.
4. **Composite**. Silhouette, Contrast, the duotone Tint, the Torch in linear light, the
   aura (outside the subject, with streamers along its outward gradient), exposure
   flicker and the vignette, the grain layers, the freeze's grey and flash, the caption,
   Mix.

**The grain is the enlargement's evidence.** Each generation leaves a layer of grain frozen
in its print, and that layer is enlarged with the picture from then on: layer j's cell is
`grainCell x m / m( j )`, its weight how far past generation j the depth is. All layers
are anchored at the affine map's fixed point, so a frozen layer grows about the same
point the picture does. The live layer (new every film frame, 24 a second) is always last.
`bbtest --grain` checks the variance adds a layer per generation and the old layers are
coarse.

**The caption is a signed distance field.** `tools/make-font.py` bakes Roboto Condensed
at weight 300 into a 704x336 R8 atlas (`source/FontAtlas.cpp`, committed); the composite
draws each placed glyph from it, with the halo from the same field. Letters reveal on a
three-letter ramp as Caption Show rises. Legend adds a catalogue number that moves on with
each print.

**Time.** Every animation runs on `elapsed`, the sum of this instance's own clamped dt, so a
host clock that jumps moves nothing by more than a quarter of a second. The host clock's
unit (seconds or milliseconds) is voted on against the wall clock, as rosette does; the
harness declares seconds instead (`SetClockScaleForTest`).

## Decisions taken without asking

- **The name** is Bluebook, after Project Blue Book (US Air Force, 1952 to 1969), whose
  name is a public record. ID `BK01`, `SW Bluebook`.
- **Nothing from the show.** No footage, music, logo, lettering or text. The default
  caption is "NOTHING TO SEE HERE", not the show's tagline. The README names The X-Files
  and NCIS once each, to say what the look is and that the plugin isn't affiliated; the
  plugin's own description string in the host names neither.
- **The font.** Fonts In Use identifies the original's lettering as Helios Condensed
  Light, which nobody may redistribute. Roboto Condensed (OFL 1.1) at weight 300 is the
  nearest open face; it's embedded as a distance field and keeps its name.
- **The freeze lives here**, as the Freeze group, because it was asked for as an addition
  to this plugin. It works with every other stage off.
- **The freeze captures the print stage's output**, not the finished picture, so
  enlargement, stretch, grade and grain keep working on a frozen frame and Freeze Push
  goes through the same affine map as the enlargement.
- **Defaults.** Print and Film are on (what the sequence did to every shot: eight prints a
  second, a short dissolve, a little weave, teal, grain, flicker, vignette). Everything
  that belongs to one shot (Enlarge, Stretch, Aura, Torch, Caption, Freeze) starts off.
- **Caption Show is a slider**, not a button, so the reveal can be run from a timeline or
  a fader.
- **Grain Size** is 1 to 4 px at 1080 lines and scales with the raster, so the grain is
  the same on the film whatever the output size.
- **Trail is RGBA32F** (see the traps). It costs memory: at 4K the two trail buffers are
  265 MB, about a third of the 760 MB or so the plugin allocates there (556 MB of colour,
  and about 205 MB of depth; see Not done).

## The traps actually hit

1. **A resize gave a black first frame.** Fresh buffers were detected by a new GL id on
   the held prints, and the driver handed back the id it had just freed, so nothing was
   reseeded and the print stage smeared from empty buffers. Fresh is now decided by size.
   `bbtest --resize` caught it.
2. **A one-pole in half floats stops short.** Once a frame's step is under half a ULP it
   rounds to nothing: at Trail 1 the smear stalled 6 % short of where it was going, which
   is a ghost of every bright thing that never leaves. The trail is RGBA32F.
3. **The software renderer blended in a stale mip level.** The framed picture is
   mipmapped (the aura's mask reads level 2), and a 1:1 `texture()` read of it is at level
   0 only if the rasteriser's LOD comes out at exactly 0. Apple's software renderer's
   comes out a hair above and blended in level 1, which is regenerated only when the aura
   is on: last frame's picture, or, after another instance had freed its memory, NaNs.
   Every read now names its level (`level0()`), and the 1:1 ones don't filter at all.
4. **The software renderer's `uv` lands a hair off the texel centres**, so a filtered 1:1
   read blurs a sharp edge by a few thousandths and the clip no longer comes back as
   itself. Every 1:1 read is a `texelFetch` at `texelOf( size )`; the frame pass takes the
   `texelFetch` path when its map is exactly the identity.
5. **`mix( a, b, 1.0 )` isn't promised to be `b`**, and on the software renderer it isn't:
   the live clip's last bits leaked through a frozen frame and through Mix 1. The freeze
   and both ends of Mix are chosen, not mixed.
6. **The aura's streamers made worms.** With the Dark key and a low threshold, a broad
   half-keyed area of sky had a flat glow whose gradient was noise, and streamers along
   noise are worms. Streamers are now weighted by the glow's edge strength (`len x sigma`,
   0.8 at a blurred step's edge). A dark subject also kept 30 % of its own glow and turned
   grey; with the Dark key it keeps none. And the outward direction had been skewed by
   multiplying the x difference by the aspect ratio, when glow texels are already square.
7. **The corona was first drawn inside the subject**, where the glow is flat and there's
   no outward to stream along. It's weighted by `1 - subject`.
8. **Two checks were unreachable.** `--alpha` and `--caption` are the harness's PNG and
   text options, so `bbtest --alpha` wrote a PNG and passed. The checks are
   `--transparency` and `--lettering`.
9. **A check that sat at a fixed point.** The freeze check's test frame had lumas near
   0.5 and 1, where the grey's smoothstep is the identity, so a mutant that changed the
   curve survived. The frame now ramps from dark to light.
10. **This GPU's conversion to half float isn't round-to-nearest.** The error reached a
    whole ULP, so the tolerances that pass through a half-float buffer are one ULP.
11. **The weave's bound includes the overscan.** A print is enlarged by
    `1 / ( 1 - 2 weave )` before it's offset, so it can sit 2.5 % further off than Weave
    alone says.

## Verified, and assumed

Verified offline, on this Mac (Apple Silicon, macOS 26), on its GPU and on Apple's
software renderer: every row of the README's Status table. `tools/verify.sh` is the
record; it rebuilds universal and runs everything.

Assumed, because nothing here can check it:

- How Resolume shows a text parameter (Caption) and a slider used as a reveal.
- That Resolume's `SetBeatInfo` bar phase runs 0 to 1 over a bar and wraps at the
  downbeat. Step On Beat and Bar count bars from the wrap.
- That the host's input texture holds the clip at its origin, as FFGL says, and is the
  size of the raster the plugin renders. Every buffer is sized from the input's
  Width and Height.
- That an Intel Mac's GPU behaves as the two renderers here do. The x86_64 slice is
  built, signed and loaded by `oxbow` on this Mac, never run on Intel.

## Would this hold on another rasteriser, at another raster?

- Every picture check runs at 640x360 and 320x180, and every tolerance is derived (a
  half-float ULP, a centroid's quarter pixel, the quarter-raster glow's 4 px box and
  tent), never tuned to pass.
- All 120 checks pass on Apple's software renderer, which found traps 3 to 5.
- No `sin`, `cos`, `tan` or `atan` in the GLSL (`verify.sh` greps): the torch's path is
  worked out on the CPU, and every shape is an `exp` or a `smoothstep`.
- Not tried: Mesa, or any Windows GL. There's no Windows build.

## The negative controls and mutants

`bbtest --negative` runs 13 checks against a deliberately wrong model (a shader hook that
squashes the stretch, glows inside the subject, offsets the torch, hangs glyphs from their
cells' bottoms, keeps the freeze's colour, snaps the dissolve or flattens the grain; or a
model with the wrong rate, half-life, depth, weave or curve) and requires each to fail.
`tools/mutate.sh` changes one character of the shipped code 17 times, in the GLSL and the
C++, and requires the named check to fail every time. `--resize`, `--transparency` and
`--state` have no negative; a mutant that breaks them shows as a black frame or a GL error.

## The sweep's honest list

`tools/sweep.py --bare` (no per-control context) finds 26 controls that do nothing on
their own, because their stage is off by default: Magnify, Generations, Stretch Width,
Writhe, Stretch X, Stretch Y, Aura Size, Aura Key, Aura Threshold, Aura Colour, Corona,
Torch Size, Torch Sweep, Torch Angle, Torch X, Torch Y, Caption, Caption Style, Caption
Size, Caption Spacing, Caption Glow, Caption Y, Freeze Hold, Freeze Fade, Freeze Flash and
Freeze Push. With their stage on, all 50 controls change the picture.

Enlarge X and Enlarge Y aren't on that list: at Enlarge 0 they still move the grain's
anchor (the affine map has no fixed point, so the anchor falls back to the target). The
live grain is new every film frame, so nobody can see it.

## Not done, and open questions

- **Never loaded into Resolume.** The first Arena run should check the inspector's
  groups and names, Step On against Arena's transport, Caption as a text field, and
  Freeze against a host's own Bypass.
- **Naming.** The README names The X-Files and NCIS once each, nominatively. The site
  dropped product names from `/hardware` at Allan's request; if he'd rather the same
  here, it's two sentences in the README and the Inspirations entries in
  `ATTRIBUTIONS.md`.
- **Not registered in stoatworks-backend yet.** The repo is public at
  `stoatworks-labs/bluebook` (no CI yet), but there's no website entry, so
  `source/StoatworksAbout*.h` and `ATTRIBUTIONS.md` are hand copies until the backend
  generates them, and the README has no downloads block.
- No Windows build, user guide, demo video or presets.
- The trail's memory at 4K (see Decisions). Allocating it in half float when Trail is 0
  would halve that, at the price of a reallocation when Trail is first raised.
- **Every pass buffer has a depth buffer nobody uses.** `FFGLFBO::Initialise` always adds
  a 24-bit depth renderbuffer, and nothing here draws with depth: nine of them, about
  205 MB at 4K. `PassBuffer` came from schlieren, and on 2 November 2026 65 plugins under
  `~/Projects/resolume` built theirs through the same `Initialise` (conway, honeydew,
  langton, slipstream, stencil and toolpath don't). So the fix (allocate without one)
  is a sweep across the fleet, not a change to this repo alone.
