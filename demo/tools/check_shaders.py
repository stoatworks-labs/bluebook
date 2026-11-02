"""The demo's shaders and caption font must be the plugin's, character for character.

    python3 demo/tools/check_shaders.py           compare; exit 1 on any drift
    python3 demo/tools/check_shaders.py --write   regenerate demo/shaders.js and demo/fontatlas.js

Called from `tools/verify.sh`. Exit code 1 means a copy has drifted.

------------------------------------------------------------------- why

The page runs two things copied out of the plugin rather than ported: the GLSL
and the caption's typeface.

`demo/shaders.js` holds the eight GLSL pieces of `source/Shaders.cpp` (the
kCommon library, kQuadVertex, and the capture, print, frame, mask, blur and
composite bodies), kVersion, and CONSTANTS: the eight `const int` lines that
`Constants()` writes between the version line and kCommon, from the numbers in
Shaders.h and Font.h. `demo/fontatlas.js` holds the font: Font.h's numbers,
FontAtlas.cpp's advances, and its 704 x 336 signed-distance atlas as base64.
That is two copies of each, and copies drift -- quietly, because a caption
drawn from a *plausible* atlas, or a grade from a *plausible* shader, looks
exactly like one drawn from the right one. `bbtest` drives the real plugin
class and has never heard of this page, and `tools/glslc.sh` looks only at the
C++ copies.

------------------------------------------------------------------- what it does

Shaders: pulls each `R"( ... )"` body out of `source/Shaders.cpp` and the
matching backtick literal out of `demo/shaders.js`, and compares them exactly
-- no whitespace normalisation, no comment stripping: a comment updated on one
side only is exactly the drift worth catching. `kVersion`, a plain string, is
compared too. The page does not change the version line: the kit's `port()`
does that at compile time, as it does for every demo in the fleet (the version
line and the ES precision defaults, nothing else).

CONSTANTS is rebuilt from `Constants()` as it stands in Shaders.cpp: the
lambda that writes a line is pinned, every `line( "name", symbol )` call is
read in order, and each symbol's value is looked up in Shaders.h or Font.h. A
new call, a renamed symbol or a changed number changes the expected text.

It fails when Shaders.cpp grows a piece this script does not list (a pass the
page would silently lack), and it checks the JOIN as well as the pieces:
`Assemble()` is kVersion + Constants() + kCommon + the pass, InitGL in
Bluebook.cpp builds the vertex stage as kVersion + kQuadVertex and compiles
one fragment stage per Pass, in Shaders.h's order; plugin.js must assemble
them the same way. A change to either side's assembly fails here and needs a
reader.

The one transformation is a decode: a backtick cannot appear raw in a template
literal, so --write would escape one as \\`. There are none in the C++ today,
and any other backslash or `${` on the JS side is rejected.

Font: every `constexpr` number in Font.h is compared, by name, with the
page's; the 96 advances are compared literal for literal, as text, with the
C++ `f` dropped, and each is checked, exactly, in rationals, to become the
same float both ways (the page parses the literal to a double and rounds that
to a float, where clang rounds it straight to a float, and the two can differ
on a midpoint); and the atlas is decoded from base64 and compared byte for
byte with FontAtlas.cpp's array, whose initialiser must have exactly
kAtlasWidth x kAtlasHeight entries (C++ zero-fills a short one silently).

`--write` produces both files from the C++ by exactly those transformations,
so the copies are spliced by this script and never typed.

------------------------------------------------------------------- what it cannot

Nothing here checks the PORTED half: Controls.cpp (the mappings), the frame
logic of Bluebook.cpp (the print clock, the beat, the weave, the generations,
the freeze, the torch, the flicker, the grain layers, every uniform) and
LayOutCaption are a hand translation in demo/plugin.js. demo/tools/check_port.mjs
holds that to the plugin, frame by frame, against `bbtest --trace`; when you
change one of those, change the port too and run it.
"""
import base64
import os
import re
import struct
import sys
from fractions import Fraction

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.dont_write_bytecode = True

# JS constant, C++ symbol (all in source/Shaders.cpp), in the order they are
# declared there.
SHADERS = [
    ("QUAD_VERTEX", "kQuadVertex"),
    ("COMMON", "kCommon"),
    ("CAPTURE_FRAGMENT", "kCaptureFragment"),
    ("PRINT_FRAGMENT", "kPrintFragment"),
    ("FRAME_FRAGMENT", "kFrameFragment"),
    ("MASK_FRAGMENT", "kMaskFragment"),
    ("BLUR_FRAGMENT", "kBlurFragment"),
    ("COMPOSITE_FRAGMENT", "kCompositeFragment"),
]

# Constants()'s line writer, as it stands in Shaders.cpp. The calls after it
# are read, not pinned; this is what turns each call into text.
CONSTANTS_LAMBDA = ('\tauto line = []( const char* name, int value ) { return std::string( "const int " ) + name'
                    ' + " = " + std::to_string( value ) + ";\\n"; };\n')

# How the pieces are joined, on each side. Each entry is (file, text that must
# appear in it verbatim). The C++ lines are the plugin's assembly; the JS lines
# are plugin.js's copy of it. A change to either is a change to what runs.
ASSEMBLY = [
    ("source/Shaders.cpp",
     "\tstd::string out = kVersion;\n"
     "\tout += Constants();\n"
     "\tout += kCommon;\n"
     "\tswitch( pass )\n"
     "\t{\n"
     "\tcase Pass::Capture: out += kCaptureFragment; break;\n"
     "\tcase Pass::Print: out += kPrintFragment; break;\n"
     "\tcase Pass::Frame: out += kFrameFragment; break;\n"
     "\tcase Pass::Mask: out += kMaskFragment; break;\n"
     "\tcase Pass::Blur: out += kBlurFragment; break;\n"
     "\tcase Pass::Composite: out += kCompositeFragment; break;\n"
     "\tcase Pass::Count: break;\n"
     "\t}\n"
     "\treturn out;\n"),
    ("source/Shaders.h",
     "enum class Pass\n{\n\tCapture,\n\tPrint,\n\tFrame,\n\tMask,\n\tBlur,\n\tComposite,\n\tCount\n};"),
    ("source/Bluebook.cpp", "const std::string vertex = std::string( shaders::kVersion ) + shaders::kQuadVertex;"),
    ("source/Bluebook.cpp", "for( int p = 0; p < static_cast< int >( shaders::Pass::Count ); ++p )"),
    ("source/Bluebook.cpp", "const std::string fragment = shaders::Assemble( pass );"),
    ("demo/plugin.js", "  const vertex = S.VERSION + S.QUAD_VERTEX;\n"),
    ("demo/plugin.js", "  const assemble = (piece) => S.VERSION + S.CONSTANTS + S.COMMON + piece;\n"),
    ("demo/plugin.js",
     "    capture: new Program(gl, vertex, assemble(S.CAPTURE_FRAGMENT), 'capture'),\n"
     "    print: new Program(gl, vertex, assemble(S.PRINT_FRAGMENT), 'print'),\n"
     "    frame: new Program(gl, vertex, assemble(S.FRAME_FRAGMENT), 'frame'),\n"
     "    mask: new Program(gl, vertex, assemble(S.MASK_FRAGMENT), 'mask'),\n"
     "    blur: new Program(gl, vertex, assemble(S.BLUR_FRAGMENT), 'blur'),\n"
     "    composite: new Program(gl, vertex, assemble(S.COMPOSITE_FRAGMENT), 'composite'),\n"),
    # The atlas goes up as FontAtlas.cpp holds it: R8, bottom row first.
    ("source/Bluebook.cpp",
     "glTexImage2D( GL_TEXTURE_2D, 0, GL_R8, font::kAtlasWidth, font::kAtlasHeight, 0, GL_RED, GL_UNSIGNED_BYTE, font::kAtlas );"),
    ("demo/plugin.js",
     "  gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, FONT.kAtlasWidth, FONT.kAtlasHeight, gl.RED, gl.UNSIGNED_BYTE, atlasBytes());\n"),
]

SHADERS_HEADER = """// GENERATED from source/Shaders.cpp (and Constants()'s numbers, from
// source/Shaders.h and source/Font.h) by demo/tools/check_shaders.py --write.
// Do not edit: tools/verify.sh fails if a character of this differs from the
// plugin's. The version line is the plugin's too; the kit's port() swaps it
// for GLSL ES 3.00's at compile time.
"""

FONT_HEADER = """// GENERATED from source/Font.h and source/FontAtlas.cpp by
// demo/tools/check_shaders.py --write. Do not edit: tools/verify.sh fails if a
// number, an advance or a byte of the atlas differs from the plugin's.
//
// Roboto Condensed Light (SIL Open Font License 1.1: fonts/OFL.txt in the
// repository), baked by tools/make-font.py into a signed distance field. The
// names are Font.h's. The advances are FontAtlas.cpp's own literals with the
// C++ float suffix dropped; the atlas is its kAtlas, R8, kAtlasWidth x
// kAtlasHeight, bottom row first, as base64.
"""


def read(*parts):
    with open(os.path.join(REPO, *parts)) as handle:
        return handle.read()


def first_difference(a, b):
    left, right = a.splitlines(), b.splitlines()
    for i in range(max(len(left), len(right))):
        x = left[i] if i < len(left) else "<missing>"
        y = right[i] if i < len(right) else "<missing>"
        if x != y:
            return i + 1, x, y
    return None


# ------------------------------------------------------------------- shaders

def cpp_version(source):
    match = re.search(r'const char\* const kVersion = "(.*?)";', source)
    return None if match is None else match.group(1)


def from_cpp(source, symbol):
    """The piece's text: its raw strings, joined as the compiler joins them."""
    match = re.search(r'const char\* const ' + symbol + r' = ((?:R"\(.*?\)"\s*)+);', source, re.S)
    if match is None:
        return None, 0
    parts = re.findall(r'R"\((.*?)\)"', match.group(1), re.S)
    rebuilt = re.sub(r'R"\(.*?\)"', "", match.group(1), flags=re.S)
    if rebuilt.strip() != "":
        return None, 0
    return "".join(parts), len(parts)


def cpp_pieces(source):
    return re.findall(r'const char\* const (k\w+) = R"\(', source)


def from_js(source, name):
    match = re.search(r'^export const ' + name + r' = `(.*?)`;$', source, re.S | re.M)
    if match is None:
        return None, None
    body = match.group(1)
    stray = re.search(r"\\(?!`)", body)
    if stray is not None:
        line = body[: stray.start()].count("\n") + 1
        return None, f"backslash that is not an escaped backtick, at line {line}"
    if "${" in body:
        return None, "template substitution"
    return body.replace("\\`", "`"), None


def int_constants(source):
    """Every `constexpr int name = value;` in a header, by name."""
    return {name: int(value) for name, value in re.findall(r'^\s*constexpr int (\w+)\s*=\s*(-?\d+);', source, re.M)}


def constants_text(shaders_cpp, shaders_h, font_h):
    """What Constants() returns, rebuilt from its own calls; or (None, why)."""
    match = re.search(r'^std::string Constants\(\)\n\{\n(.*?)\n\}$', shaders_cpp, re.M | re.S)
    if match is None:
        return None, "Constants() not found in source/Shaders.cpp"
    body = match.group(1) + "\n"
    if not body.startswith(CONSTANTS_LAMBDA):
        return None, "Constants()'s line writer has changed"
    rest = body[len(CONSTANTS_LAMBDA):]
    calls = re.findall(r'line\( "(\w+)", (font::)?(\w+) \)', rest)
    leftover = re.sub(r'line\( "\w+", (font::)?\w+ \)', "", rest)
    if re.sub(r'[\s+;]|return', "", leftover) != "":
        return None, "Constants() does something besides joining line( ... ) calls"
    shader_values, font_values = int_constants(shaders_h), int_constants(font_h)
    out = []
    for name, in_font, symbol in calls:
        values = font_values if in_font else shader_values
        if symbol not in values:
            return None, f"Constants() reads {'font::' if in_font else ''}{symbol}, which is not a constexpr int"
        out.append(f"const int {name} = {values[symbol]};\n")
    return "".join(out), len(calls)


def write_shaders(cpp, shaders_h, font_h):
    out = [SHADERS_HEADER]
    version = cpp_version(cpp)
    if version is None:
        print("FAIL  kVersion not found in source/Shaders.cpp")
        return 1
    out.append(f"export const VERSION = '{version}';\n")
    constants, why = constants_text(cpp, shaders_h, font_h)
    if constants is None:
        print(f"FAIL  {why}")
        return 1
    out.append(f"\n// Constants(), source/Shaders.cpp: the numbers from source/Shaders.h and source/Font.h\n"
               f"export const CONSTANTS = `{constants}`;\n")
    for name, symbol in SHADERS:
        body, count = from_cpp(cpp, symbol)
        if body is None:
            print(f"FAIL  {symbol} not found in source/Shaders.cpp (or not a run of adjacent raw strings)")
            return 1
        if "\\" in body or "${" in body:
            print(f"FAIL  {symbol} holds a backslash or ${{; the escape scheme cannot carry it")
            return 1
        joined = f", {count} adjacent raw strings joined" if count > 1 else ""
        out.append(f"\n// {symbol}, source/Shaders.cpp{joined}\nexport const {name} = `{body.replace('`', chr(92) + '`')}`;\n")
    with open(os.path.join(REPO, "demo", "shaders.js"), "w") as handle:
        handle.write("".join(out))
    print("wrote demo/shaders.js")
    return 0


def check_shaders(cpp, shaders_h, font_h, js):
    problems = 0
    version_cpp = cpp_version(cpp)
    version_js = re.search(r"^export const VERSION = '(.*?)';$", js, re.M)
    if version_cpp is None or version_js is None or version_cpp != version_js.group(1):
        print("FAIL  VERSION does not match kVersion")
        problems += 1
    else:
        print(f"ok    {'VERSION':<20} matches kVersion")

    want, count = constants_text(cpp, shaders_h, font_h)
    got, complaint = from_js(js, "CONSTANTS")
    if want is None:
        print(f"FAIL  {count}")
        problems += 1
    elif complaint is not None or got is None:
        print(f"FAIL  CONSTANTS missing from demo/shaders.js{'' if complaint is None else ' (' + complaint + ')'}")
        problems += 1
    elif got != want:
        print("FAIL  CONSTANTS is not what Constants() writes today")
        where = first_difference(want, got)
        if where:
            print(f"          C++: {where[1]}")
            print(f"          js : {where[2]}")
        problems += 1
    else:
        print(f"ok    {'CONSTANTS':<20} matches Constants() ({count} lines, numbers from Shaders.h and Font.h)")

    listed = {symbol for _, symbol in SHADERS}
    for symbol in cpp_pieces(cpp):
        if symbol not in listed:
            print(f"FAIL  source/Shaders.cpp defines {symbol}, which this check does not list -- "
                  "add it to SHADERS (and to the page, or say on the page why not)")
            problems += 1

    for name, symbol in SHADERS:
        cpp_text, count = from_cpp(cpp, symbol)
        js_text, complaint = from_js(js, name)
        if cpp_text is None:
            print(f"FAIL  {symbol} not found in source/Shaders.cpp (or not a run of adjacent raw strings)")
            problems += 1
        elif complaint is not None:
            print(f"FAIL  {name} in demo/shaders.js has a {complaint}")
            problems += 1
        elif js_text is None:
            print(f"FAIL  {name} not found in demo/shaders.js")
            problems += 1
        elif cpp_text == js_text:
            joined = f", {count} raw strings joined" if count > 1 else ""
            print(f"ok    {name:<20} matches {symbol} ({len(cpp_text)} chars{joined})")
        else:
            problems += 1
            print(f"FAIL  {name} has drifted from {symbol}")
            where = first_difference(cpp_text, js_text)
            if where:
                print(f"        first difference at line {where[0]}")
                print(f"          C++: {where[1]}")
                print(f"          js : {where[2]}")

    for path, text in ASSEMBLY:
        try:
            body = read(*path.split("/"))
        except FileNotFoundError:
            print(f"FAIL  {path} is missing")
            problems += 1
            continue
        first = text.strip().splitlines()[0][:60]
        if text in body:
            print(f"ok    assembly: {path} still says {first}")
        else:
            print(f"FAIL  assembly: {path} no longer says: {first} ...")
            problems += 1
    return problems


# ------------------------------------------------------------------- the font

FONT_INTS = ["kEm", "kSpread", "kCellWidth", "kCellHeight", "kColumns", "kFirst", "kCount",
             "kAtlasWidth", "kAtlasHeight", "kBaseline", "kPenX"]
FONT_FLOATS = ["kCapHeight"]


def font_floats(font_h):
    return {name: value for name, value in re.findall(r'^\s*constexpr float (\w+)\s*=\s*(-?[\d.]+)f;', font_h, re.M)}


def cpp_advances(atlas_cpp):
    match = re.search(r'^const float kAdvance\[ kCount \] = \{\n(.*?)\n\};$', atlas_cpp, re.M | re.S)
    if match is None:
        return None
    items = [item.strip() for item in match.group(1).replace("\n", " ").split(",") if item.strip()]
    if not all(re.fullmatch(r'-?\d+\.\d+f', item) for item in items):
        return None
    return [item[:-1] for item in items]


def cpp_atlas(atlas_cpp):
    match = re.search(r'^const unsigned char kAtlas\[ kAtlasWidth \* kAtlasHeight \] = \{\n(.*?)\n\};$', atlas_cpp, re.M | re.S)
    if match is None:
        return None
    items = [item.strip() for item in match.group(1).replace("\n", " ").split(",") if item.strip()]
    if not all(re.fullmatch(r'\d{1,3}', item) and int(item) < 256 for item in items):
        return None
    return bytes(int(item) for item in items)


def f32_bits(value):
    return struct.unpack("<I", struct.pack("<f", value))[0]


def f32_of_bits(bits):
    return struct.unpack("<f", struct.pack("<I", bits))[0]


def round_trips(literal):
    """The float JavaScript stores is the float the C++ compiler makes.

    The page parses the literal to the nearest double and Math.fround rounds
    that to the nearest float; clang rounds the literal straight to the nearest
    float. The two differ only when the double lands exactly on a midpoint
    between two floats the decimal itself is not on. Decided exactly, in
    rationals: the float the page gets must be the one nearest the literal's
    exact value (ties to even), against both of its neighbours.
    """
    exact = Fraction(literal)
    page = struct.unpack("<f", struct.pack("<f", float(literal)))[0]
    bits = f32_bits(page)
    best = None
    for candidate in (bits - 1, bits, bits + 1):
        if candidate < 0 or candidate >= 1 << 32:
            continue
        value = f32_of_bits(candidate)
        if value != value or value in (float("inf"), float("-inf")):
            continue
        distance = abs(Fraction(value) - exact)
        key = (distance, candidate & 1)
        if best is None or key < best[0]:
            best = (key, candidate)
    return best is not None and best[1] == bits


def write_font(font_h, atlas_cpp):
    ints, floats = int_constants(font_h), font_floats(font_h)
    advances, atlas = cpp_advances(atlas_cpp), cpp_atlas(atlas_cpp)
    if any(name not in ints for name in FONT_INTS) or any(name not in floats for name in FONT_FLOATS):
        print("FAIL  a number this script carries is missing from source/Font.h")
        return 1
    if advances is None or atlas is None:
        print("FAIL  kAdvance or kAtlas not found in source/FontAtlas.cpp in the shape this script reads")
        return 1
    out = [FONT_HEADER, "\n"]
    for name in FONT_INTS:
        out.append(f"export const {name} = {ints[name]};\n")
    for name in FONT_FLOATS:
        out.append(f"export const {name} = {floats[name]};\n")
    out.append("\n// kAdvance, source/FontAtlas.cpp: each glyph's advance in ems, codes kFirst onwards.\n")
    out.append("export const kAdvance = [\n")
    for i in range(0, len(advances), 8):
        out.append("  " + ", ".join(advances[i:i + 8]) + ",\n")
    out.append("];\n")
    encoded = base64.b64encode(atlas).decode("ascii")
    out.append("\n// kAtlas, source/FontAtlas.cpp: R8, bottom row first, base64.\n")
    out.append("export const kAtlasBase64 =\n")
    lines = [encoded[i:i + 120] for i in range(0, len(encoded), 120)]
    out.append(" +\n".join(f"  '{line}'" for line in lines) + ";\n")
    with open(os.path.join(REPO, "demo", "fontatlas.js"), "w") as handle:
        handle.write("".join(out))
    print(f"wrote demo/fontatlas.js ({len(atlas)} atlas bytes)")
    return 0


def check_font(font_h, atlas_cpp, js):
    problems = 0
    ints, floats = int_constants(font_h), font_floats(font_h)
    page_ints = {name: int(value) for name, value in re.findall(r'^export const (k\w+) = (-?\d+);$', js, re.M)}
    page_floats = {name: value for name, value in re.findall(r'^export const (k\w+) = (-?\d+\.\d+);$', js, re.M)}

    unlisted = [name for name in list(ints) + list(floats) if name not in FONT_INTS + FONT_FLOATS]
    for name in unlisted:
        print(f"FAIL  source/Font.h declares {name}, which this check does not carry -- add it to FONT_INTS or FONT_FLOATS")
        problems += 1
    bad = [name for name in FONT_INTS if name not in ints or page_ints.get(name) != ints[name]]
    bad += [name for name in FONT_FLOATS if name not in floats or page_floats.get(name) != floats[name]]
    if bad:
        print(f"FAIL  Font.h's numbers differ from the page's: {', '.join(bad)}")
        problems += 1
    else:
        print(f"ok    {'font numbers':<20} match Font.h ({len(FONT_INTS) + len(FONT_FLOATS)})")

    advances = cpp_advances(atlas_cpp)
    match = re.search(r'^export const kAdvance = \[\n(.*?)\];$', js, re.M | re.S)
    page = None if match is None else [item.strip() for item in match.group(1).replace("\n", " ").split(",") if item.strip()]
    if advances is None:
        print("FAIL  kAdvance not found in source/FontAtlas.cpp in the shape this script reads")
        problems += 1
    elif page is None:
        print("FAIL  kAdvance not found in demo/fontatlas.js")
        problems += 1
    elif len(advances) != ints.get("kCount") or page != advances:
        print(f"FAIL  kAdvance differs ({len(page)} on the page, {len(advances)} in the C++, kCount {ints.get('kCount')})")
        problems += 1
    else:
        rounding = [literal for literal in advances if not round_trips(literal)]
        if rounding:
            print(f"FAIL  kAdvance: {', '.join(rounding)} round to a different float in the page than in the C++")
            problems += 1
        else:
            print(f"ok    {'kAdvance':<20} matches ({len(advances)} literals, each the same float both ways)")

    atlas = cpp_atlas(atlas_cpp)
    match = re.search(r'^export const kAtlasBase64 =\n(.*?);$', js, re.M | re.S)
    want_size = ints.get("kAtlasWidth", 0) * ints.get("kAtlasHeight", 0)
    if atlas is None:
        print("FAIL  kAtlas not found in source/FontAtlas.cpp in the shape this script reads")
        problems += 1
    elif len(atlas) != want_size:
        print(f"FAIL  kAtlas has {len(atlas)} initialisers, not kAtlasWidth x kAtlasHeight = {want_size} "
              "(C++ would zero-fill the rest)")
        problems += 1
    elif match is None:
        print("FAIL  kAtlasBase64 not found in demo/fontatlas.js")
        problems += 1
    else:
        pieces = re.findall(r"'([A-Za-z0-9+/=]*)'", match.group(1))
        leftover = re.sub(r"'[A-Za-z0-9+/=]*'", "", match.group(1))
        page_atlas = None
        if re.sub(r'[\s+]', "", leftover) == "":
            try:
                page_atlas = base64.b64decode("".join(pieces), validate=True)
            except ValueError:
                page_atlas = None
        if page_atlas is None:
            print("FAIL  kAtlasBase64 in demo/fontatlas.js is not a run of quoted base64 pieces")
            problems += 1
        elif page_atlas != atlas:
            where = next((i for i in range(min(len(atlas), len(page_atlas))) if atlas[i] != page_atlas[i]),
                         min(len(atlas), len(page_atlas)))
            print(f"FAIL  the atlas differs: {len(page_atlas)} bytes on the page, {len(atlas)} in the C++, "
                  f"first difference at byte {where}")
            problems += 1
        else:
            print(f"ok    {'kAtlas':<20} matches, byte for byte ({len(atlas)} bytes, "
                  f"{ints['kAtlasWidth']} x {ints['kAtlasHeight']})")
    return problems


def main(argv):
    shaders_cpp = read("source", "Shaders.cpp")
    shaders_h = read("source", "Shaders.h")
    font_h = read("source", "Font.h")
    atlas_cpp = read("source", "FontAtlas.cpp")
    if "--write" in argv:
        return write_shaders(shaders_cpp, shaders_h, font_h) or write_font(font_h, atlas_cpp)
    problems = 0
    try:
        problems += check_shaders(shaders_cpp, shaders_h, font_h, read("demo", "shaders.js"))
    except FileNotFoundError:
        print("FAIL  demo/shaders.js is missing -- run demo/tools/check_shaders.py --write")
        problems += 1
    try:
        problems += check_font(font_h, atlas_cpp, read("demo", "fontatlas.js"))
    except FileNotFoundError:
        print("FAIL  demo/fontatlas.js is missing -- run demo/tools/check_shaders.py --write")
        problems += 1
    print()
    if problems:
        print(f"{problems} problem(s) -- rerun demo/tools/check_shaders.py --write for a drifted copy "
              "(never edit shaders.js or fontatlas.js by hand); an assembly or unlisted-piece "
              "failure needs a reader")
        return 1
    print(f"all {len(SHADERS) + 2} shader pieces and their assembly, and the caption font's "
          f"numbers, advances and atlas, are the plugin's")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
