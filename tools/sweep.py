"""Render every parameter at both ends of its range and fail if any made no
difference.

**This is the only thing in the repo that catches a dead control.** A GLSL
uniform whose name does not match the C++ is silently ignored --
`glGetUniformLocation` returns -1 and `glUniform` on -1 is a documented no-op
-- so a slider can be stone dead while everything compiles, links, loads and
renders.

Each control is swept where it can act: a stage's settings with the stage
turned on (Magnify with Enlarge at 1, Writhe with Stretch on, every Caption
control with Caption Show at 1), Torch Angle while the torch sweeps, Torch X
and Y while it stands still. Freeze is a press, swept by pressing it (`--at`)
and comparing with not pressing it; its settings are swept with a press at the
frame where each one shows. Every render is 150 frames (2.5 s at 60 fps) of
the harness's moving card at 120 BPM, at 320x180. An option is set by its
index. A control whose ends differ by less than `--floor` (mean 8-bit
difference per channel) is reported as barely alive.

Usage::

    tools/sweep.py [--build BUILD_DIR] [--verbose] [--jobs N] [--bare]

`--bare` drops every per-control context, which lists the controls that only
act with another one set (AGENTS.md records them).
"""

import argparse
import concurrent.futures
import pathlib
import re
import subprocess
import sys
import tempfile
import zlib

REPO = pathlib.Path(__file__).resolve().parent.parent

BASE = []

ENLARGE = ["Enlarge=1"]
STRETCH = ["Stretch=0.6"]
AURA = ["Aura=0.8"]
TORCH = ["Torch=1"]
CAPTION = ["Caption Show=1"]


def press(frame, *more):
    return [["--at", str(frame), "Freeze=1"], *more]


# name -> (low settings, high settings, context settings)
SWEEP = {
    "Stutter": (["Stutter=0"], ["Stutter=1"], []),
    "Step On": (["Step On=0"], ["Step On=1"], []),
    "Step Rate": (["Step Rate=0.2"], ["Step Rate=0.8"], []),
    "Dissolve": (["Dissolve=0"], ["Dissolve=1"], []),
    "Weave": (["Weave=0"], ["Weave=1"], []),
    "Trail": (["Trail=0"], ["Trail=0.8"], []),
    "Enlarge": (["Enlarge=0"], ["Enlarge=0.8"], []),
    "Enlarge Run": (["Enlarge Run=0"], ["Enlarge Run=1"], []),
    "Magnify": (["Magnify=0.2"], ["Magnify=0.8"], ENLARGE),
    "Generations": (["Generations=1"], ["Generations=8"], ENLARGE),
    "Enlarge X": (["Enlarge X=0.2"], ["Enlarge X=0.8"], ENLARGE),
    "Enlarge Y": (["Enlarge Y=0.2"], ["Enlarge Y=0.8"], ENLARGE),
    "Stretch": (["Stretch=0"], ["Stretch=0.6"], []),
    "Stretch Width": (["Stretch Width=0.2"], ["Stretch Width=0.8"], STRETCH),
    "Writhe": (["Writhe=0"], ["Writhe=1"], STRETCH),
    "Stretch X": (["Stretch X=0.3"], ["Stretch X=0.7"], STRETCH),
    "Stretch Y": (["Stretch Y=0.3"], ["Stretch Y=0.7"], STRETCH),
    "Aura": (["Aura=0"], ["Aura=0.8"], []),
    "Aura Size": (["Aura Size=0.2"], ["Aura Size=0.8"], AURA),
    "Aura Key": (["Aura Key=0"], ["Aura Key=1"], AURA),
    "Aura Threshold": (["Aura Threshold=0.2"], ["Aura Threshold=0.8"], AURA),
    "Aura Colour": (["Aura Colour=0"], ["Aura Colour=3"], AURA),
    "Corona": (["Corona=0"], ["Corona=1"], AURA),
    "Silhouette": (["Silhouette=0"], ["Silhouette=1"], []),
    "Torch": (["Torch=0"], ["Torch=1"], []),
    "Torch Size": (["Torch Size=0.2"], ["Torch Size=0.8"], TORCH),
    "Torch Sweep": (["Torch Sweep=0"], ["Torch Sweep=0.8"], TORCH),
    "Torch Angle": (["Torch Angle=0"], ["Torch Angle=0.25"], TORCH + ["Torch Sweep=0.5"]),
    "Torch X": (["Torch X=0.3"], ["Torch X=0.7"], TORCH + ["Torch Sweep=0"]),
    "Torch Y": (["Torch Y=0.3"], ["Torch Y=0.7"], TORCH + ["Torch Sweep=0"]),
    "Tint": (["Tint=0"], ["Tint=3"], []),
    "Tint Amount": (["Tint Amount=0"], ["Tint Amount=1"], []),
    "Contrast": (["Contrast=0"], ["Contrast=1"], []),
    "Grain": (["Grain=0"], ["Grain=1"], []),
    "Grain Size": (["Grain Size=0"], ["Grain Size=1"], ["Grain=1"]),
    "Flicker": (["Flicker=0"], ["Flicker=1"], []),
    "Vignette": (["Vignette=0"], ["Vignette=1"], []),
    "Caption": (["Caption=IIII"], ["Caption=WWWW"], CAPTION),
    "Caption Show": (["Caption Show=0"], ["Caption Show=1"], []),
    "Caption Style": (["Caption Style=0"], ["Caption Style=1"], CAPTION),
    "Caption Size": (["Caption Size=0.2"], ["Caption Size=0.8"], CAPTION),
    "Caption Spacing": (["Caption Spacing=0"], ["Caption Spacing=1"], CAPTION),
    "Caption Glow": (["Caption Glow=0"], ["Caption Glow=1"], CAPTION),
    "Caption Y": (["Caption Y=0.3"], ["Caption Y=0.7"], CAPTION),
    "Freeze": ([], press(100), []),
    # A 0.25 s hold has let go by the last frame; a 2 s one has not.
    "Freeze Hold": (["Freeze Hold=0"], ["Freeze Hold=0.6"], press(60)),
    # Nine frames after the press: grey at once, or a tenth of the way.
    "Freeze Fade": (["Freeze Fade=0"], ["Freeze Fade=1"], press(140)),
    "Freeze Flash": (["Freeze Flash=0"], ["Freeze Flash=1"], press(145)),
    "Freeze Push": (["Freeze Push=0"], ["Freeze Push=1"], press(100, "Freeze Hold=1")),
    "Mix": (["Mix=0"], ["Mix=1"], []),
}

# The About block: a text line and browser buttons, with no pixel to sweep.
SKIP = {"About", "Project page", "Source on GitHub", "Support the work", "User guide"}


def read_png(path):
    """Enough of PNG for bbtest's own writer: 8-bit RGBA, filter 0 rows."""
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    pos, width, height, idat = 8, 0, 0, b""
    while pos < len(data):
        length = int.from_bytes(data[pos:pos + 4], "big")
        kind = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            width = int.from_bytes(body[0:4], "big")
            height = int.from_bytes(body[4:8], "big")
        elif kind == b"IDAT":
            idat += body
        pos += 12 + length
    raw = zlib.decompress(idat)
    stride = width * 4
    out = bytearray()
    for y in range(height):
        start = y * (stride + 1)
        out += raw[start + 1:start + 1 + stride]
    return bytes(out)


def difference(a, b):
    if len(a) != len(b):
        return 255.0
    return sum(abs(x - y) for x, y in zip(a, b)) / len(a)


def render(bbtest, out, settings):
    args = [str(bbtest), "--out", str(out), "--size", "320x180", "--frames", "150"]
    for setting in settings:
        args += list(setting) if isinstance(setting, list) else ["--set", setting]
    result = subprocess.run(args, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"bbtest failed: {' '.join(args)}\n{result.stderr.strip()}")
    return read_png(out)


def parameters(bbtest):
    result = subprocess.run([str(bbtest), "--list"], capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"bbtest --list failed: {result.stderr.strip()}")
    names = []
    for line in result.stdout.splitlines():
        parts = re.split(r"\s{2,}", line.strip())
        if len(parts) >= 3 and parts[0].isdigit():
            names.append(parts[1].strip())
    return names


def sweep_one(bbtest, scratch, name, bare):
    low, high, context = SWEEP[name]
    context = BASE + ([] if bare else context)
    tag = re.sub(r"\W", "_", name)
    before = render(bbtest, scratch / f"{tag}-a.png", context + low)
    after = render(bbtest, scratch / f"{tag}-b.png", context + high)
    return name, difference(before, after)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=pathlib.Path, default=REPO / "build")
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("--bare", action="store_true")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--floor", type=float, default=0.05,
                        help="mean 8-bit difference below which a control is 'barely alive'")
    args = parser.parse_args()

    build = args.build if args.build.is_absolute() else REPO / args.build
    bbtest = build / "bbtest"
    if not bbtest.exists():
        print(f"{bbtest} not found", file=sys.stderr)
        return 1

    declared = parameters(bbtest)
    unknown = [n for n in declared if n not in SWEEP and n not in SKIP]
    if unknown:
        # A new parameter with no sweep is a hole, not a pass.
        print(f"no sweep defined for: {', '.join(unknown)}", file=sys.stderr)
        return 1
    unused = [n for n in SWEEP if n not in declared]
    if unused:
        print(f"the sweep names parameters the plugin does not have: {', '.join(unused)}", file=sys.stderr)
        return 1

    dead, weak = [], []
    with tempfile.TemporaryDirectory() as scratch:
        scratch = pathlib.Path(scratch)
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
            results = list(pool.map(lambda n: sweep_one(bbtest, scratch, n, args.bare),
                                    [n for n in declared if n in SWEEP]))
    for name, delta in results:
        if delta == 0.0:
            dead.append(name)
            print(f"  DEAD {name:16s} both ends identical")
        elif delta < args.floor:
            weak.append(name)
            print(f"  WEAK {name:16s} mean delta {delta:.4f}")
        elif args.verbose:
            print(f"  ok   {name:16s} mean delta {delta:.3f}")

    print(f"{len(results)} parameters swept{' (bare: base context only)' if args.bare else ''}, "
          f"{len(dead)} dead, {len(weak)} barely alive")
    if dead or weak:
        if not args.bare:
            print("\nA parameter that changes nothing is usually a uniform name that does not match\n"
                  "the C++, or a setting nothing reads. Both are silent everywhere else.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
