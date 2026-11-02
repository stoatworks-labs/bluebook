# Attributions

Bluebook is built on other people's work. This file lists what that work is, who did
it, and what it is doing here.

Once the repo is registered in `stoatworks-backend`, this file is generated there by
`scripts/sync-attributions.py`. Until then it's a hand copy in the same shape.

## Code we derived from other people's work

Someone else solved this first, and this project would not exist in its current form without their work.

### The effect's shape, the GL state guard, the pass buffers, the log and the tooling — Stoatworks schlieren

<https://github.com/stoatworks-labs/schlieren>  
Licence: MIT  
Copyright: Stoatworks Labs

The plugin class's shape and its table of controls, source/GLState.h, source/PassBuffer.{h,cpp} (FFGLFBO with the SDK's colour-texture leak fixed; millpond's), source/Diag.{h,cpp}, cmake/Info.plist.in, and the shape of the harness (its rig, PNG writer, cue sheets, --pipe/--film, --negative with its Perturb, --state) and of tools/verify.sh, tools/mutate.sh and tools/sweep.py are schlieren's, which had them from fiche and millpond. All adapted. The host-clock unit vote in BluebookPlugin::UpdateClock is rosette's.

## Third-party code this project uses

Libraries, SDKs, frameworks and assets the project is built on or bundles.

### Resolume FFGL SDK

<https://github.com/resolume/ffgl>  
Licence: BSD-3-Clause  
Copyright: FreeFrame

Vendored as a git submodule at external/ffgl, pinned at b1afaf9.

The plugin ABI itself. An FFGL effect is defined by this SDK's headers — there is no other way to be loadable by Resolume Arena and Avenue.

### Roboto Condensed

<https://github.com/googlefonts/roboto-classic>  
Licence: SIL Open Font License 1.1  
Copyright: 2011 The Roboto Project Authors

The caption's lettering: printable ASCII at weight 300, baked by tools/make-font.py into a signed distance field in source/FontAtlas.cpp. The licence and notice are in fonts/OFL.txt. The font is embedded in the plugin, never sold on its own, and keeps its name.

### zlib

<https://zlib.net>  
Licence: zlib  
Copyright: Jean-loup Gailly and Mark Adler

The macOS system copy, linked by the harness (tools/bbtest) only, to read and write PNGs. The plugin doesn't use it.

## Work we checked ourselves against

### Fast third-order texture filtering — Christian Sigg and Markus Hadwiger, GPU Gems 2, ch. 20 (2005)

The cubic B-spline read in the frame pass (bspline() in source/Shaders.cpp): four bilinear taps instead of sixteen, which is what a print enlarged by an optical printer looks like rather than a bilinear smear.

### Improved alpha-tested magnification for vector textures and special effects — Chris Green, Valve, SIGGRAPH 2007

The caption's signed distance field: one small atlas draws a crisp letter at any size, and the same field gives the halo.

### Hash functions for GPU rendering — Mark Jarzynski and Marc Olano, JCGT 9(3), 2020; PCG — Melissa O'Neill

pcg() in source/Shaders.cpp and the harness: the integer hash behind the grain, the corona's noise and the weave, the same on every GPU.

## Inspirations

### The opening titles of The X-Files (1993) — main title sequence by Castle/Bryant/Johnsen

The treatments this plugin recreates. No footage, music, logo, lettering or text from the sequence is used, and the plugin isn't affiliated with or endorsed by anyone who made it. Read about it at Art of the Title (<https://www.artofthetitle.com/title/the-x-files>), Reflections on Film and Television (<https://reflectionsonfilmandtelevision.blogspot.com/2014/06/outre-intro-x-files-1993-2002.html>) and Fonts In Use (<https://fontsinuse.com/uses/9064/the-x-files-main-title>).

### The freeze-frame scene transition of NCIS (2003–)

The Freeze group: a press freezes the frame with a flash and drains it to black and white. Not affiliated with or endorsed by anyone who made the show.

### Project Blue Book (US Air Force, 1952–1969)

The name.

## Standards and published specifications

What the implementation is measured against.

- **IEC 61966-2-1 (sRGB)** — toLinear() and toCode() in source/Shaders.cpp: the torch multiplies light, so it works in linear light and encodes again. bbtest --torch checks the room keeps 3 % of its light through both curves, the linear toe included.
- **ITU-R BT.709 luma** — luma() in source/Shaders.cpp (0.2126, 0.7152, 0.0722): the duotone's position, the Aura's Bright and Dark keys and the freeze's grey. bbtest --grade checks two colours of the same luma tint alike.
- **The Gaussian blur of a step** — the aura's glow is the subject's mask blurred by a Gaussian of Aura Size, so outside an edge it falls as erfc( x / ( sigma sqrt 2 ) ) / 2; bbtest --aura checks the fall from one sigma to two.
- **The one-pole low-pass** — Trail: y += ( x - y ) ( 1 - 0.5 ^ ( dt / half-life ) ); bbtest --trail checks a step's rise against 1 - 0.5 ^ ( t / half-life ).

## Getting this wrong

If your work is here and the description is inaccurate, the licence is wrong, or you would rather not be listed — open an issue and it will be fixed.
