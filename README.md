# bluebook

> **AI-assisted project.** This codebase was created with [Claude](https://claude.com/claude-code)
> (Anthropic), directed and reviewed by a human author. Its behaviour is measured rather
> than asserted: an offline harness drives the plugin's own class in a headless GL context
> and checks each claim against the picture. Prints land on the frame and the beat the
> model says, the smear rises as its half-life to a ULP, an enlargement magnifies by what
> it says to a quarter of a pixel, the aura falls off as a blurred edge does, the caption's
> capitals are the height asked for to the pixel, and a freeze holds the frame bit for bit.
> 13 negative controls and 17 one-character mutants prove the checks can fail. It's
> **never been loaded into Resolume**. The full rundown is under [Status](#status).

The look of a 1990s paranormal TV title sequence, and a crime-drama freeze-frame hit, as
an FFGL effect for [Resolume](https://resolume.com) Arena and Avenue.

![A night sky over a dark ridge, graded cold teal and grainy. A pale figure stands with one arm raised, and the figure, its open hand and a small light in the sky each glow blue, with fine streamers of light reaching outwards. A grid in the top left glows too. Across the bottom, in thin spaced capitals with a soft halo: NOTHING TO SEE HERE](docs/hero.png)

<sub>One frame, rendered by `bbtest`, the offline harness, not captured from Resolume. The
harness's own test card (a figure, a light in the sky, a grid and colour bars) on the
defaults with Aura at 0.55, Corona at 0.7 and the caption shown.</sub>

## The idea

If you watched television in the nineties you'll know the one I mean: thirty-odd seconds
of jittery, grainy footage that looked as if someone had smuggled it out of a filing
cabinet. The opening titles of The X-Files (1993) are a run of treatments rather than a
story (a stuttering print, a zoom into a photograph until it's all grain, a face pulled
long, a hand that glows, torches in the dark, a line of text spelling itself out) and
every one of them is something a VJ could use on any clip. So this plugin does the
treatments and leaves the footage to you. There's no footage, logo, music or lettering
from the show in here, and it isn't affiliated with anyone who made it.

It's named after Project Blue Book, the US Air Force's study of UFO reports from 1952 to
1969, which filed some twelve thousand sightings and concluded there was nothing to see.
Hence the default caption.

## What it does

Each group is a stage, and they stack in this order. Everything that belongs to one shot
starts switched off; the defaults are what the sequence did to every shot.

- **Print.** The clip, step-printed: a still every eighth of a second (or on the beat),
  held, with a short dissolve into the next one. Each print sits a hair off where the
  last one did, as film does in a tired gate. Trail smears the result, if you want the
  streaky version.
- **Enlarge.** The zoom into a photograph to find the thing in it. Each generation is a
  new enlargement of the last print, softer than the one before, and each carries the
  grain it was printed with, enlarged with it. Four generations in, the grain is coarse
  and clumpy and the picture is mostly guesswork, which is rather the point.
- **Stretch.** The face pulled long in a silent scream: a column of the picture drawn
  out vertically, pinched in at the waist, writhing a little.
- **Aura.** The glowing hand. Whatever is brightest (or darkest, or the clip's alpha)
  gets a glow round it, not on it, with streamers that run outwards from its edges.
  Silhouette darkens the subject itself, for the black shape against the light.
- **Torch.** A dark room with a beam crossing it, lit in linear light so the hot middle
  looks like a lamp and not a grey disc. It sweeps on its own or stays where you put it.
- **Film.** The grade: a cold teal duotone (or green, blue, amber, silver), crushed
  contrast, grain, an exposure that flickers frame by frame and a dark edge.
- **Caption.** A line of text revealed a letter at a time, in thin spaced capitals with
  a halo, centred as a title or right-aligned as an evidence legend with a catalogue
  number that moves on with every print.
- **Freeze.** The other show's hit. Press it and the frame on screen freezes with a white
  flash, drains to punchy black and white and pushes in slightly, then lets go after
  Freeze Hold, or stays until you press it again. It works on its own, with the rest
  switched off.

## Controls

| Group | |
| --- | --- |
| **Print** | Stutter (0 the live clip, 1 the prints), Step On (Rate, Beat, 1/2 Beat, 1/4 Beat, Bar), Step Rate (1–30 prints a second), Dissolve (the share of each print spent dissolving in), Weave (0–1.2 % of the frame height), Trail (a smear with a half-life of up to 1.5 s). |
| **Enlarge** | Enlarge (how many generations in), Enlarge Run (Manual, or Cycle: one generation per print, then back to the start), Magnify (1.5–24× at the last generation), Generations (1–8), Enlarge X, Enlarge Y (what it zooms towards). |
| **Stretch** | Stretch (1–5× down the centre of the column), Stretch Width (4–80 % of the frame height), Writhe, Stretch X, Stretch Y. |
| **Aura** | Aura, Aura Size (the glow's reach, 0.3–12 % of the frame height), Aura Key (Bright, Dark, Alpha), Aura Threshold, Aura Colour (Blue, White, Green, Red, Violet), Corona (the streamers), Silhouette. |
| **Torch** | Torch, Torch Size (4–80 % of the frame height), Torch Sweep (0 stands still, then 20 s to 0.5 s a pass), Torch Angle (0–360°), Torch X, Torch Y. |
| **Film** | Tint (Teal, Green, Blue, Amber, Silver, Natural), Tint Amount, Contrast, Grain, Grain Size (1–4 px at 1080 lines), Flicker, Vignette. |
| **Caption** | Caption (up to 48 characters), Caption Show (0 hidden, 1 every letter in; between, part-revealed), Caption Style (Title, Legend), Caption Size (cap height, 1.5–15 % of the frame height), Caption Spacing, Caption Glow, Caption Y. |
| **Freeze** | Freeze (press), Freeze Hold (0.25–10 s, and at the top it latches until the next press), Freeze Fade (0–1.5 s to grey), Freeze Flash, Freeze Push (0–20 %). |
| **Output** | Mix. |

Caption Show is a slider so you can run the reveal from a clip's timeline, a MIDI fader
or Resolume's own envelopes; at 1 every letter is in.

## Status

**v0.1.0, unreleased.** Built and measured on one Mac; nobody has seen it in a host yet.

### Measured offline, on macOS

`tools/verify.sh` runs every picture check at **two rasters**, 640×360 and 320×180, and
again on **Apple's software renderer** at 320×180. What the checks establish:

| check | result |
| --- | --- |
| `--identity` | every stage off returns the clip to within a half-float ULP; everything on at Mix 0 returns it **bit for bit** |
| `--print` | at 7 prints a second, Dissolve 0 and 0.5: every one of 59 frames shows the print and the dissolve the model says |
| `--beat` | Step On Beat and Bar at 120 BPM print on the host's beat, 0 of 250 frames wrong |
| `--weave` | over 20 prints at Weave 1, the print never sits further off than 1.2 % of the frame height (plus the overscan), does move, and the clip's edge never shows |
| `--trail` | at Trail 0.4 and 1, a black-to-white step rises as 1 − 0.5^(t / half-life) to within one half-float ULP for eight half-lives, and crosses half way on the half-life's frame |
| `--enlarge` | Magnify 4, three generations of four: ×2.8284 measured against 4^0.75 = 2.8284, about the target |
| `--grain` | four generations in, the grain's variance is 5.0× the live grain's (one layer per generation plus the live one), and correlated over a distance the live grain isn't |
| `--stretch` | Stretch 2: the centre column's lines at twice their distance to half a pixel, and three widths across, where they were |
| `--aura` | a glow just past the subject's edge and not on it; from one Aura Size out to two it falls as a blurred edge does, to within 15 %, at two sizes |
| `--torch` | the beam centred on its aim to a pixel, round, hottest in the middle; outside it the room keeps 3 % of its light in linear light; Torch Angle 90 sweeps upwards and crosses its aim half a pass in |
| `--grade` | Tint maps two colours of the same luma to the same duotone; Contrast 1 is smoothstep; Vignette follows its curve to a ULP |
| `--lettering` | an H is Caption Size tall and centred on Caption Y, to the pixel; a Title's ink is centred across; half shown, the first letters are in and the last aren't; Show 0 draws nothing |
| `--freeze` | the frame before the press, held **bit for bit** in grey for Freeze Hold; let go on the frame; the flash and the fade as stated; latched until the next press; Freeze Push 1 pushes in ×1.2 (×1.2001 and ×1.1998 at the two rasters) |
| `--resize`, `--transparency`, `--state` | a new raster starts clean from its first frame; clear stays clear except where the aura's light falls; the host's GL state comes back |
| `--offline` | 54 parameter names, none repeated, none over FFGL's 16 characters; every range ends where this README says; the caption's layout |
| `--negative` | **13** deliberately wrong models, **13** caught |
| mutation | **17** one-character mutants of the shipped GLSL and C++, **17** caught |
| `tools/sweep.py` | all **50** controls it can move measurably change the picture |
| shaders | all 7 compile through `glslc`, not merely through Apple's driver |
| CI | on every push, GitHub's macOS runner builds it and runs the offline checks and every picture check on its software renderer; the Windows DLL is built and loaded as a host loads it (its name, ID and 54 parameters, Caption a text field) |

The software renderer earned its keep: it found three things the GPU never showed (a mip
level blended into a 1:1 read, `uv` landing a hair off the texel centres, and
`mix(a, b, 1.0)` that isn't `b`), and all three are fixed. AGENTS.md has the details.

### Not established

It's **never been loaded into Resolume**, on any platform. Everything above was measured
offline: every picture against the plugin's own class in a headless CGL context. I
haven't seen how the 54 controls read in Arena's inspector, whether Step On follows
Arena's transport, or how a host's own Bypass and Opacity sit with Freeze. The
Windows DLL comes out of CI and answers a host's first questions correctly, but it's never
drawn a frame. There's no user guide and no presets.

The harness's test card is drawn, not filmed, so how the Aura key copes with real
footage (where a bright sky is as "bright" as a hand) is a matter of setting Aura Key
and Aura Threshold to taste, and I haven't tried it on much.

## Build

macOS (universal: Apple Silicon and Intel):

```bash
git clone --recurse-submodules https://github.com/stoatworks-labs/bluebook
```

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build
```

```bash
cmake --install build
```

The last one copies `Bluebook.bundle` into `~/Documents/Resolume Arena/Extra Effects`.
Restart Arena and it's under Effects as **SW Bluebook**.

Windows needs Visual Studio 2022 and vcpkg (for GLEW); see `.github/workflows/ci.yml`.

## Building and testing

```bash
tools/verify.sh
```

That's the whole lot: a fresh universal build, every check at two rasters and on the
software renderer, the negative controls, the mutants, the sweep for dead controls, the
bundle's architecture and plist, an ad-hoc signature, a host's view of it through
`oxbow` if you have it, and the render cost. `CLAUDE.md` lists the checks one by one.

Render a frame of your own:

```bash
./build/bbtest --out /tmp/bluebook.png --set Aura=0.6 --set "Caption Show=1" --caption "EXHIBIT 7: A SMUDGE"
```

## License

MIT. See [LICENSE](LICENSE) and [ATTRIBUTIONS.md](ATTRIBUTIONS.md).

<!-- attributions:start -->
This project is built on other people's work — see [ATTRIBUTIONS.md](ATTRIBUTIONS.md).
<!-- attributions:end -->
