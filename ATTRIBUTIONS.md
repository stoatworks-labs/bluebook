# Attributions

Bluebook is built on other people's work. This file lists what that work is, who did
it, and what it is doing here.

It is generated — the master lists live in the `stoatworks-backend` repo and are
pushed out by `scripts/sync-attributions.py`. Edit it there, not here.

## Code we derived from other people's work

Someone else solved this first, and this project would not exist in its current form without their work.

### The effect's shape, the GL state guard, the pass buffers, the log and the tooling — Stoatworks schlieren

<https://github.com/stoatworks-labs/schlieren>  
Licence: MIT  
Copyright: Stoatworks Labs

The plugin class's shape and its table of controls, source/GLState.h, source/PassBuffer.{h,cpp} (FFGLFBO with the SDK's colour-texture leak fixed; millpond's), source/Diag.{h,cpp}, cmake/Info.plist.in, and the shape of the harness (its rig, PNG writer, cue sheets, --pipe/--film, --negative with its Perturb, --state) and of tools/verify.sh, tools/mutate.sh and tools/sweep.py are schlieren's, which had them from fiche and millpond. All adapted. The host-clock unit vote in BluebookPlugin::UpdateClock is rosette's.

## Third-party code this project uses

Libraries, SDKs and frameworks the project is built on or bundles.

### Resolume FFGL SDK

<https://github.com/resolume/ffgl>  
Licence: BSD-3-Clause  
Copyright: FreeFrame

Vendored as a git submodule at external/ffgl (third_party/ffgl in oxbow).

The plugin ABI itself. An FFGL effect or source is defined by this SDK's headers — there is no other way to be loadable by Resolume Arena and Avenue.

### GLEW — the OpenGL Extension Wrangler Library

<https://github.com/nigels-com/glew>  
Licence: BSD-3-Clause (with Mesa 3-D and Khronos components)  
Copyright: Milan Ikits, Marcelo E. Magallon and Lev Povalahev

Arrives inside the FFGL submodule at external/ffgl/deps/glew-2.1.0. Not fetched separately.

Resolves OpenGL entry points on Windows, where the system headers stop at OpenGL 1.1.

### libpng

<http://www.libpng.org/pub/png/libpng.html>  
Licence: PNG Reference Library License (libpng)  
Copyright: the PNG Reference Library authors

Arrives inside the FFGL submodule, under the SDK's CustomThumbnail sample.

Part of the upstream SDK tree rather than something these plugins call directly — listed because it is present in the checkout.

### Roboto Condensed

<https://github.com/googlefonts/roboto-classic>  
Licence: SIL Open Font License 1.1  
Copyright: The Roboto Project Authors

Printable ASCII at weight 300, baked by the repo's own tool into a signed distance field compiled into the plugin. The licence and notice are committed as fonts/OFL.txt and ship in every download.

Lettering a plugin can draw at any size from one small atlas, with nothing to install. The OFL allows embedding it in software, provided it is never sold on its own.

## Work we checked ourselves against

No code was taken from these — but they were how we knew we had it right, and that is worth saying out loud.

### Fast third-order texture filtering — C. Sigg and M. Hadwiger, GPU Gems 2, chapter 20 (2005)

The cubic B-spline read in the frame pass (bspline() in source/Shaders.cpp): four bilinear taps instead of sixteen, which is what a print enlarged by an optical printer looks like rather than a bilinear smear.

### Improved alpha-tested magnification for vector textures and special effects — C. Green, Valve, SIGGRAPH 2007

The caption's signed distance field: one small atlas draws a crisp letter at any size, and the same field gives the halo.

### Hash functions for GPU rendering — M. Jarzynski and M. Olano, JCGT 9 (2020); PCG by M. O'Neill

<https://jcgt.org/published/0009/03/02/>

pcg() in source/Shaders.cpp and the harness: the integer hash behind the grain, the corona's noise and the weave, the same on every GPU.

## Inspirations

What this set out to be. No code, assets or binaries from any of these were used or examined — the debt is to the idea.

### The opening titles of The X-Files (1993) — main title sequence by Castle/Bryant/Johnsen

<https://www.artofthetitle.com/title/the-x-files>

The treatments this plugin recreates. No footage, music, logo, lettering or text from the sequence is used, and the plugin isn't affiliated with or endorsed by anyone who made it. Also read at Reflections on Film and Television (https://reflectionsonfilmandtelevision.blogspot.com/2014/06/outre-intro-x-files-1993-2002.html) and Fonts In Use (https://fontsinuse.com/uses/9064/the-x-files-main-title).

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
