/**
 * Bluebook — browser demo.
 *
 * The treatments of a 1993 paranormal TV title sequence as stages that stack,
 * every frame, in one order: a step printer (Print), a photo-interpretation
 * zoom (Enlarge), the face pulled long (Stretch), the glowing hand (Aura), a
 * torch in the dark (Torch), the grade (Film) and a caption spelling itself
 * out; and a crime drama's freeze-frame hit (Freeze). SW Bluebook (BK01) is an
 * FFGL effect: it takes the clip and hands back the treated picture.
 *
 * Two halves, and they are not equally faithful here:
 *
 *   **The GPU half is the plugin's own GLSL.** `shaders.js` is the eight
 *   `R"( ... )"` pieces of `source/Shaders.cpp`, kVersion, and the `const int`
 *   lines Constants() writes between them, spliced by
 *   `demo/tools/check_shaders.py --write` and never typed; the same script
 *   compares them character for character and checks that they are joined as
 *   `Assemble()` and InitGL join them, and `tools/verify.sh` runs it. They run
 *   as the plugin's six passes on the plugin's buffers: two RGBA16F held
 *   prints, two RGBA32F trail buffers ping-ponging, an RGBA16F frozen frame, an
 *   RGBA16F mipmapped framed picture, and three R16F quarter-size glow
 *   buffers.
 *
 *   **The caption's typeface is the plugin's data.** `fontatlas.js` is Font.h's
 *   numbers, FontAtlas.cpp's advances and its 704 x 336 signed-distance atlas
 *   (base64), spliced and compared byte for byte by the same script.
 *
 *   **The CPU half is a port.** Between the PORT markers below: Controls.cpp
 *   (the table, every mapping and default), Font.h's AdvanceOf, and the
 *   helpers at the top of Bluebook.cpp (the PCG hash, the tints, the aura's
 *   colours, the beat's divisions). Between the FRAME markers: Bluebook.cpp's
 *   SetFloatParameter, SetTextParameter, SetTime, SetBeatInfo, UpdateSteps,
 *   LayOutCaption and the CPU side of ProcessOpenGL (the print clock, the
 *   weave, the dissolve and the trail's decay, the freeze, the generations and
 *   their mis-registration, the affine map, the flicker, the torch's sweep,
 *   the grain layers and every uniform), split into `Plan()`, which touches
 *   no GL and is what `demo/tools/check_port.mjs` holds to `bbtest --trace`
 *   frame by frame, and `BluebookGL.draw()`, which runs the passes in the
 *   order ProcessOpenGL runs them. JavaScript's numbers are doubles, as the
 *   plugin's CPU half is; the host's parameters are floats there, so every
 *   control is rounded to a float on its way in here, and so are the bar
 *   phase and the BPM.
 *
 * ---------------------------------------------------------------------------
 * Decisions this page made, and why
 * ---------------------------------------------------------------------------
 *
 * **One instance, as a layer holds one.** It is made when the page loads and
 * lives as long as the page. Changing the Composition size is the host's
 * raster changing under a running instance: the plugin decides a fresh set of
 * buffers by their size, so the prints, the freeze and the smear all start
 * again from that frame, here as there.
 *
 * **The transport is simulated, as the plugin's harness's is.** Step On's
 * Beat, 1/2 Beat, 1/4 Beat and Bar print on Resolume's beat. The page keeps a
 * BPM clock (the Host BPM field under the picture, 120 at first) and hands the
 * plugin that BPM and a bar phase advanced from the page's own seconds every
 * frame, through the port of SetBeatInfo, as `bbtest` does (ferrofluid's page
 * is the precedent). The phase is accumulated, so a new BPM changes the rate
 * from then on and never jumps it; Restart puts it back to the top of a bar.
 *
 * **The clock.** The kit's: seconds from frame deltas, capped at a tenth of a
 * second a frame, handed over as the host's time already in seconds, so the
 * plugin's seconds-or-milliseconds vote (UpdateClock, after rosette) is not
 * ported; `bbtest` declares seconds the same way (SetClockScaleForTest). The
 * first frame has no time (dt 0), as in the plugin. Restart takes the clock
 * back, which the plugin clamps to no time passing; the prints carry on from
 * where they were.
 *
 * **Freeze is a toggle.** It is FF_TYPE_EVENT; the kit has no event control,
 * so it is a toggle the page lets go of after one frame: one rising edge,
 * which is what SetFloatParameter counts as a press.
 *
 * **Generations is a dropdown.** It is FF_TYPE_INTEGER, 1 to 8; the kit has
 * no integer control, so (as colourunder, cogwheel and boreal do) it is a
 * dropdown of the same eight values, and the page hands the plugin the
 * integer, not the index.
 *
 * **Caption goes in as UTF-8 bytes.** The plugin keeps its text as a
 * std::string of whatever bytes the host sends, turns each UTF-8 character
 * outside printable ASCII into one `?` (Printable), and keeps the first 48. The
 * page encodes the field as UTF-8 and hands the plugin those bytes, which is an
 * assumption about what a host sends: nobody has typed a caption into Resolume
 * yet (AGENTS.md).
 *
 * **The harness's test hooks are not ported**: Hooks is 0, as it is in a
 * shipped instance. The About block is not a parameter here, as on every page
 * in the suite.
 */

import { mountDemo } from './vendor/demo.js';
import { Program, PassBuffer, GLError, bindTexture } from './vendor/gl.js';
import * as S from './shaders.js';
import * as FONT from './fontatlas.js';

// ==== BEGIN PORT ===========================================================
// Controls.h / Controls.cpp, Font.h's AdvanceOf, and the anonymous namespace
// at the top of Bluebook.cpp, ported. No DOM and no GL between the markers.

const f32 = Math.fround;
/** std::clamp, std::min and std::max exactly: comparisons, so -0 stays -0. */
const clamp = (v, lo, hi) => (v < lo ? lo : hi < v ? hi : v);
const min2 = (a, b) => (b < a ? b : a);
const max2 = (a, b) => (a < b ? b : a);
/** std::lround: half away from zero (Math.round alone rounds -2.5 to -2). */
const lround = (v) => (v < 0 ? -Math.round(-v) : Math.round(v));
const kPi = 3.14159265358979323846;

//---------------------------------------------------------------------------
// Controls.h: ParamId, in the order Resolume shows them. The About block is
// last; the page declares none of it.
//---------------------------------------------------------------------------

const PT = {
  STUTTER: 0, STEP_ON: 1, STEP_RATE: 2, DISSOLVE: 3, WEAVE: 4, TRAIL: 5,
  ENLARGE: 6, ENLARGE_RUN: 7, MAGNIFY: 8, GENERATIONS: 9, ENLARGE_X: 10, ENLARGE_Y: 11,
  STRETCH: 12, STRETCH_WIDTH: 13, WRITHE: 14, STRETCH_X: 15, STRETCH_Y: 16,
  AURA: 17, AURA_SIZE: 18, AURA_KEY: 19, AURA_THRESHOLD: 20, AURA_COLOUR: 21, CORONA: 22, SILHOUETTE: 23,
  TORCH: 24, TORCH_SIZE: 25, TORCH_SWEEP: 26, TORCH_ANGLE: 27, TORCH_X: 28, TORCH_Y: 29,
  TINT: 30, TINT_AMOUNT: 31, CONTRAST: 32, GRAIN: 33, GRAIN_SIZE: 34, FLICKER: 35, VIGNETTE: 36,
  CAPTION: 37, CAPTION_SHOW: 38, CAPTION_STYLE: 39, CAPTION_SIZE: 40, CAPTION_SPACING: 41, CAPTION_GLOW: 42, CAPTION_Y: 43,
  FREEZE: 44, FREEZE_HOLD: 45, FREEZE_FADE: 46, FREEZE_FLASH: 47, FREEZE_PUSH: 48,
  MIX: 49,
  ABOUT_FIRST: 50,
};

const Kind = { Standard: 0, Option: 1, Integer: 2, Text: 3, Event: 4, XPos: 5, YPos: 6 };

const StepOn = { Rate: 0, Beat: 1, HalfBeat: 2, QuarterBeat: 3, Bar: 4, Count: 5 };
const EnlargeRun = { Manual: 0, Cycle: 1, Count: 2 };
const AuraKey = { Bright: 0, Dark: 1, Alpha: 2, Count: 3 };
const AuraColour = { Blue: 0, White: 1, Green: 2, Red: 3, Violet: 4, Count: 5 };
const Tint = { Teal: 0, Green: 1, Blue: 2, Amber: 3, Silver: 4, Natural: 5, Count: 6 };
const CaptionStyle = { Title: 0, Legend: 1, Count: 2 };

/** OptionIndex(): the host stores the index; a non-finite one is 0. */
function OptionIndex(value, count) {
  if (!Number.isFinite(value)) return 0;
  return clamp(lround(value), 0, count - 1);
}

const kDefaultCaption = 'NOTHING TO SEE HERE';
/** The longest caption drawn, in characters (bytes); the rest is dropped. */
const kMaxCaption = 48;

//---------------------------------------------------------------------------
// Controls.cpp: the mappings. Each takes the host's float.
//---------------------------------------------------------------------------

const clamp01 = (v) => clamp(v, 0.0, 1.0);
const geometric = (v, low, high) => low * Math.pow(high / low, clamp01(v));
function inverseGeometric(value, low, high) {
  const lo = min2(low, high);
  const hi = max2(low, high);
  return f32(Math.log(clamp(value, lo, hi) / low) / Math.log(high / low));
}
const linear = (v, low, high) => low + (high - low) * clamp01(v);

const kStepRateLow = 1.0;
const kStepRateHigh = 30.0;
const kMagnifyLow = 1.5;
const kMagnifyHigh = 24.0;
const kStretchWidthLow = 0.04;
const kStretchWidthHigh = 0.8;
const kAuraLow = 0.003;
const kAuraHigh = 0.12;
const kTorchLow = 0.04;
const kTorchHigh = 0.8;
const kSweepSlow = 20.0;
const kSweepFast = 0.5;
const kCaptionLow = 0.015;
const kCaptionHigh = 0.15;
const kHoldLow = 0.25;
const kHoldHigh = 10.0;

/** Prints a second at Step On Rate: 1 to 30, geometrically. */
const StepRateFromParam = (v) => geometric(v, kStepRateLow, kStepRateHigh);
const ParamFromStepRate = (perSecond) => inverseGeometric(perSecond, kStepRateLow, kStepRateHigh);
/** Weave: 0 to 1.2 % of the frame height, linearly. */
const WeaveFromParam = (v) => linear(v, 0.0, 0.012);
/** Trail: the smear's half-life, 0 to 1.5 s, quadratically. */
function TrailHalfLifeFromParam(value) {
  const v = clamp01(value);
  return 1.5 * v * v;
}
/** Magnify: the last generation's, 1.5x to 24x, geometrically. */
const MagnifyFromParam = (v) => geometric(v, kMagnifyLow, kMagnifyHigh);
const ParamFromMagnify = (times) => inverseGeometric(times, kMagnifyLow, kMagnifyHigh);
/** Stretch: 1x to 5x, linearly. */
const StretchFromParam = (v) => linear(v, 1.0, 5.0);
/** Stretch Width: 0.04 to 0.8 frame heights, geometrically. */
const StretchWidthFromParam = (v) => geometric(v, kStretchWidthLow, kStretchWidthHigh);
/** Aura Size: the glow's sigma, 0.3 % to 12 % of the frame height, geometrically. */
const AuraSizeFromParam = (v) => geometric(v, kAuraLow, kAuraHigh);
const ParamFromAuraSize = (heights) => inverseGeometric(heights, kAuraLow, kAuraHigh);
/** Torch Size: the beam's 1/e radius, 4 % to 80 % of the frame height, geometrically. */
const TorchSizeFromParam = (v) => geometric(v, kTorchLow, kTorchHigh);
const ParamFromTorchSize = (heights) => inverseGeometric(heights, kTorchLow, kTorchHigh);
/** Torch Sweep: exactly 0 stands still; above it, 20 s down to 0.5 s a pass. */
function TorchSweepSecondsFromParam(value) {
  if (!(value > 0.0)) return 0.0;
  return geometric(value, kSweepSlow, kSweepFast);
}
/** Torch Angle: 0 to 360 degrees; 0 is left to right, 90 bottom to top. */
const TorchAngleFromParam = (v) => linear(v, 0.0, 360.0);
/** Grain Size: 1 to 4 output pixels at 1080 lines, geometrically. */
const GrainSizeFromParam = (v) => geometric(v, 1.0, 4.0);
/** Caption Size: the capital height, 1.5 % to 15 % of the frame height, geometrically. */
const CaptionSizeFromParam = (v) => geometric(v, kCaptionLow, kCaptionHigh);
const ParamFromCaptionSize = (heights) => inverseGeometric(heights, kCaptionLow, kCaptionHigh);
/** Caption Spacing: 0 to 0.6 em added to every advance. */
const CaptionSpacingFromParam = (v) => linear(v, 0.0, 0.6);
/** Freeze Hold: 0.25 to 10 s, geometrically; the top of the travel latches (0). */
function FreezeHoldFromParam(value) {
  if (value >= 1.0) return 0.0;
  return geometric(value, kHoldLow, kHoldHigh);
}
/** Freeze Fade: colour to grey, 0 to 1.5 s, linearly. 0 cuts. */
const FreezeFadeFromParam = (v) => linear(v, 0.0, 1.5);
/** Freeze Push: 0 to 20 %. */
const FreezePushFromParam = (v) => linear(v, 0.0, 0.2);

//---------------------------------------------------------------------------
// Controls.cpp's kTable, indexed by ParamId. Defaults are the host's numbers:
// 0..1, the element index, or (Generations) the integer.
//---------------------------------------------------------------------------

const STEP_ON_NAMES = ['Rate', 'Beat', '1/2 Beat', '1/4 Beat', 'Bar'];
const ENLARGE_RUN_NAMES = ['Manual', 'Cycle'];
const AURA_KEY_NAMES = ['Bright', 'Dark', 'Alpha'];
const AURA_COLOUR_NAMES = ['Blue', 'White', 'Green', 'Red', 'Violet'];
const TINT_NAMES = ['Teal', 'Green', 'Blue', 'Amber', 'Silver', 'Natural'];
const CAPTION_STYLE_NAMES = ['Title', 'Legend'];

const row = (id, name, group, kind, value, extra = {}) =>
  ({ id, name, group, kind, defaultValue: f32(value), options: null, minimum: 0, maximum: 1, text: null, ...extra });
const standard = (id, name, group, value) => row(id, name, group, Kind.Standard, value);
const option = (id, name, group, value, options) => row(id, name, group, Kind.Option, value, { options });
const of = (id, name, group, kind, value) => row(id, name, group, kind, value);
const integer = (id, name, group, value, minimum, maximum) => row(id, name, group, Kind.Integer, value, { minimum, maximum });
const text = (id, name, group, value) => row(id, name, group, Kind.Text, 0, { text: value });

const INFO = [
  standard(PT.STUTTER, 'Stutter', 'Print', 1.0),
  option(PT.STEP_ON, 'Step On', 'Print', 0.0, STEP_ON_NAMES),
  standard(PT.STEP_RATE, 'Step Rate', 'Print', ParamFromStepRate(8.0)),
  standard(PT.DISSOLVE, 'Dissolve', 'Print', 0.35),
  standard(PT.WEAVE, 'Weave', 'Print', 0.3),
  standard(PT.TRAIL, 'Trail', 'Print', 0.0),

  standard(PT.ENLARGE, 'Enlarge', 'Enlarge', 0.0),
  option(PT.ENLARGE_RUN, 'Enlarge Run', 'Enlarge', 0.0, ENLARGE_RUN_NAMES),
  standard(PT.MAGNIFY, 'Magnify', 'Enlarge', ParamFromMagnify(8.0)),
  integer(PT.GENERATIONS, 'Generations', 'Enlarge', 4.0, 1.0, 8.0),
  of(PT.ENLARGE_X, 'Enlarge X', 'Enlarge', Kind.XPos, 0.5),
  of(PT.ENLARGE_Y, 'Enlarge Y', 'Enlarge', Kind.YPos, 0.5),

  standard(PT.STRETCH, 'Stretch', 'Stretch', 0.0),
  standard(PT.STRETCH_WIDTH, 'Stretch Width', 'Stretch', 0.45),
  standard(PT.WRITHE, 'Writhe', 'Stretch', 0.3),
  of(PT.STRETCH_X, 'Stretch X', 'Stretch', Kind.XPos, 0.5),
  of(PT.STRETCH_Y, 'Stretch Y', 'Stretch', Kind.YPos, 0.5),

  standard(PT.AURA, 'Aura', 'Aura', 0.0),
  standard(PT.AURA_SIZE, 'Aura Size', 'Aura', ParamFromAuraSize(0.025)),
  option(PT.AURA_KEY, 'Aura Key', 'Aura', 0.0, AURA_KEY_NAMES),
  standard(PT.AURA_THRESHOLD, 'Aura Threshold', 'Aura', 0.5),
  option(PT.AURA_COLOUR, 'Aura Colour', 'Aura', 0.0, AURA_COLOUR_NAMES),
  standard(PT.CORONA, 'Corona', 'Aura', 0.5),
  standard(PT.SILHOUETTE, 'Silhouette', 'Aura', 0.0),

  standard(PT.TORCH, 'Torch', 'Torch', 0.0),
  standard(PT.TORCH_SIZE, 'Torch Size', 'Torch', ParamFromTorchSize(0.22)),
  standard(PT.TORCH_SWEEP, 'Torch Sweep', 'Torch', 0.5),
  standard(PT.TORCH_ANGLE, 'Torch Angle', 'Torch', 0.0),
  of(PT.TORCH_X, 'Torch X', 'Torch', Kind.XPos, 0.5),
  of(PT.TORCH_Y, 'Torch Y', 'Torch', Kind.YPos, 0.5),

  option(PT.TINT, 'Tint', 'Film', 0.0, TINT_NAMES),
  standard(PT.TINT_AMOUNT, 'Tint Amount', 'Film', 0.85),
  standard(PT.CONTRAST, 'Contrast', 'Film', 0.5),
  standard(PT.GRAIN, 'Grain', 'Film', 0.35),
  standard(PT.GRAIN_SIZE, 'Grain Size', 'Film', 0.35),
  standard(PT.FLICKER, 'Flicker', 'Film', 0.3),
  standard(PT.VIGNETTE, 'Vignette', 'Film', 0.5),

  text(PT.CAPTION, 'Caption', 'Caption', kDefaultCaption),
  standard(PT.CAPTION_SHOW, 'Caption Show', 'Caption', 0.0),
  option(PT.CAPTION_STYLE, 'Caption Style', 'Caption', 0.0, CAPTION_STYLE_NAMES),
  standard(PT.CAPTION_SIZE, 'Caption Size', 'Caption', ParamFromCaptionSize(0.045)),
  standard(PT.CAPTION_SPACING, 'Caption Spacing', 'Caption', 0.4),
  standard(PT.CAPTION_GLOW, 'Caption Glow', 'Caption', 0.5),
  of(PT.CAPTION_Y, 'Caption Y', 'Caption', Kind.YPos, 0.5),

  of(PT.FREEZE, 'Freeze', 'Freeze', Kind.Event, 0.0),
  standard(PT.FREEZE_HOLD, 'Freeze Hold', 'Freeze', 0.45),
  standard(PT.FREEZE_FADE, 'Freeze Fade', 'Freeze', 0.2),
  standard(PT.FREEZE_FLASH, 'Freeze Flash', 'Freeze', 0.6),
  standard(PT.FREEZE_PUSH, 'Freeze Push', 'Freeze', 0.3),

  standard(PT.MIX, 'Mix', 'Output', 1.0),
];
if (INFO.length !== PT.ABOUT_FIRST || INFO.some((info, i) => info.id !== i)) {
  throw new Error('every control has one row, in ParamId order');
}

//---------------------------------------------------------------------------
// Font.h: the advance of a code in ems; anything unprintable is a space.
//---------------------------------------------------------------------------

const ADVANCE = new Float32Array(FONT.kAdvance);
function AdvanceOf(code) {
  if (code < FONT.kFirst || code >= FONT.kFirst + FONT.kCount - 1) code = 32;
  return ADVANCE[code - FONT.kFirst];
}

//---------------------------------------------------------------------------
// Bluebook.cpp's anonymous namespace.
//---------------------------------------------------------------------------

/** Frames that must agree before the host's clock unit is settled (not ported: see the header). */
/** Seconds of host time a single frame may advance by. */
const kMaxFrameDelta = 0.25;
/** The film the grain, the flicker and the shimmer are drawn on runs at 24. */
const kFilmRate = 24.0;
/** How fast the freeze's flash dies away, and its push settles, seconds. */
const kFlashDecay = 0.08;
const kPushSettle = 0.35;
/** Letters over which the caption's reveal ramps from nothing to full. */
const kRevealLetters = 3.0;

/** PCG's output mix, in uint32 (Math.imul and >>> 0: a double would lose the low bits). */
function pcg(v) {
  const state = (Math.imul(v, 747796405) + 2891336453) >>> 0;
  const word = Math.imul((state >>> ((state >>> 28) + 4)) ^ state, 277803737) >>> 0;
  return ((word >>> 22) ^ word) >>> 0;
}
/** The same hash as the GLSL's hash01, on the CPU: 0..1. `a` and `b` are uint32. */
function hash01(a, b) {
  return pcg((Math.imul(a, 2654435761) ^ pcg((b + 0x9e3779b9) >>> 0)) >>> 0) * (1.0 / 4294967296.0);
}
/** -1..1. static_cast< uint32_t > of a long long is its low 32 bits, which >>> 0 is. */
const signedHash = (a, b) => 2.0 * hash01(a >>> 0, b) - 1.0;

/** The tints, as code values: shadows to light along the luma. */
const kTints = [
  { dark: [0.000, 0.035, 0.050], light: [0.80, 0.97, 0.95] }, // Teal
  { dark: [0.010, 0.050, 0.020], light: [0.80, 1.00, 0.78] }, // Green
  { dark: [0.000, 0.020, 0.070], light: [0.78, 0.88, 1.00] }, // Blue
  { dark: [0.060, 0.025, 0.000], light: [1.00, 0.86, 0.60] }, // Amber
  { dark: [0.000, 0.000, 0.000], light: [0.97, 0.97, 0.97] }, // Silver
  { dark: [0.000, 0.000, 0.000], light: [1.00, 1.00, 1.00] }, // Natural: never mixed in
];

const kAuraColours = [
  [0.35, 0.60, 1.00], // Blue
  [0.90, 0.95, 1.00], // White
  [0.40, 1.00, 0.50], // Green
  [1.00, 0.25, 0.15], // Red
  [0.70, 0.40, 1.00], // Violet
];

/** Prints a beat: what each Step On but Rate divides a beat into. */
function printsPerBeat(on) {
  switch (on) {
    case StepOn.Beat: return 1.0;
    case StepOn.HalfBeat: return 2.0;
    case StepOn.QuarterBeat: return 4.0;
    case StepOn.Bar: return 0.25;
    default: return 1.0;
  }
}

/** Shaders.h. */
const kMaxGlyphs = 64;
const kMaxLayers = 9;

// ==== END PORT =============================================================

// ==== BEGIN FRAME ==========================================================
// Bluebook.cpp: the instance's state, the parameter setters, UpdateSteps,
// LayOutCaption and ProcessOpenGL. Plan() is ProcessOpenGL's CPU side, in its
// order and with no GL; BluebookGL.draw() is its passes, in theirs.

const encoder = new TextEncoder();

/**
 * Printable: the caption as the font can draw it, one byte a character. Each
 * UTF-8 character outside printable ASCII is one '?'; a byte that is not
 * valid UTF-8 is a character of its own.
 */
function Printable(bytes) {
  const out = [];
  let pending = 0;
  for (const b of bytes) {
    if (pending > 0 && (b & 0xc0) === 0x80) {
      pending -= 1;
      continue;
    }
    pending = b >= 0xf0 && b < 0xf8 ? 3 : b >= 0xe0 && b < 0xf0 ? 2 : b >= 0xc0 && b < 0xe0 ? 1 : 0;
    out.push(b >= FONT.kFirst && b <= 126 ? b : 63);
  }
  return Uint8Array.from(out);
}

/** What the CPU decided for a frame: Bluebook.h's FrameState. */
function newFrameState() {
  return {
    steps: 0, fade: 1.0, decay: 0.0, captured: false, frozen: false, grey: 0.0, flash: 0.0, push: 1.0,
    generation: 0.0, magnify: 1.0, scale: 1.0, biasX: 0.0, biasY: 0.0, torchX: 0.0, torchY: 0.0, exposure: 1.0,
    layerCount: 1, layerScale: new Float32Array(kMaxLayers), layerWeight: new Float32Array(kMaxLayers), reallocated: false,
  };
}

class BluebookPlugin {
  constructor() {
    // The constructor: every parameter at its table default, as a float.
    this.params = new Float32Array(PT.ABOUT_FIRST);
    for (let id = 0; id < PT.ABOUT_FIRST; id += 1) this.params[id] = INFO[id].defaultValue;
    this.captionText = encoder.encode(kDefaultCaption);

    // Time. The vote on the host's unit is not ported: this host sends seconds.
    this.hostTime = -1.0;
    this.now = 0.0;
    this.lastNow = -1.0;
    this.elapsed = 0.0;

    // The step printer's clock, and the beat.
    this.stepPosition = 0.0;
    this.stepCount = 0;
    this.stepPrimed = false;
    this.bpm = f32(120.0);
    this.barPhase = f32(0.0);
    this.lastBarPhase = f32(-1.0);
    this.bars = 0;
    this.hostBeatSeen = false;

    // The freeze.
    this.freezePresses = 0;
    this.freezeHeld = false;
    this.freezing = false;
    this.freezeAge = 0.0;

    this.torchPhase = 0.0;

    // ensureBuffers' bookkeeping: the held prints' size (0 is none yet), and
    // which held and trail buffer is which. The buffers are BluebookGL's.
    this.heldWidth = 0;
    this.heldHeight = 0;
    this.newest = 0;
    this.current = 0;
    this.glowWidth = 0;
    this.glowHeight = 0;

    this.state = newFrameState();
    this.layout = { glyphs: [], scale: 1.0, box: [0, 0, 0, 0] };
  }

  SetFloatParameter(index, value) {
    value = f32(value);
    if (!(index >= 0 && index < PT.ABOUT_FIRST)) return false;
    if (index === PT.CAPTION) return true;
    // Freeze acts on the press, once: 1 on press and 0 on release, either restated.
    if (index === PT.FREEZE) {
      const down = value >= 0.5;
      if (down && !this.freezeHeld) this.freezePresses += 1;
      this.freezeHeld = down;
    }
    this.params[index] = value;
    return true;
  }

  /** Caption, as the bytes a host's char* would carry. */
  SetTextParameter(index, value) {
    if (index === PT.CAPTION) this.captionText = encoder.encode(value ?? '');
  }

  SetTime(time) {
    this.hostTime = time;
  }

  SetBeatInfo(newBpm, newBarPhase) {
    newBpm = f32(newBpm);
    newBarPhase = f32(newBarPhase);
    if (newBpm > 0.0 && Number.isFinite(newBpm)) this.bpm = newBpm;
    if (Number.isFinite(newBarPhase)) this.barPhase = f32(newBarPhase - Math.floor(newBarPhase));
  }

  /** UpdateClock, with the host's unit declared as seconds (clockScale 1). */
  UpdateClock() {
    if (this.hostTime >= 0.0) this.now = this.hostTime;
  }

  UpdateSteps(dt) {
    const p = this.params;
    const on = OptionIndex(p[PT.STEP_ON], StepOn.Count);
    const prior = this.stepPosition;

    if (on === StepOn.Rate) {
      const rate = StepRateFromParam(p[PT.STEP_RATE]);
      this.stepPosition += dt * rate;
    } else {
      const perBeat = printsPerBeat(on);
      // On the host's bar when it sends one, so a print lands ON the beat;
      // on its tempo alone when it does not.
      if (this.hostBeatSeen) this.stepPosition = (this.bars + this.barPhase) * 4.0 * perBeat;
      else this.stepPosition += ((dt * this.bpm) / 60.0) * perBeat;
    }

    this.state.captured = false;
    if (!this.stepPrimed) {
      this.stepPrimed = true;
      return;
    }
    // However far the position moved, one print: there is only one frame to take it from.
    if (Math.floor(this.stepPosition) !== Math.floor(prior)) {
      this.stepCount += 1;
      this.state.captured = true;
    }
  }

  /** LayOutCaption: the caption for a raster, as ProcessOpenGL makes it. `bytes` is the text. */
  LayOutCaption(width, height, bytes, steps) {
    const p = this.params;
    const out = { glyphs: [], scale: 1.0, box: [0, 0, 0, 0] };
    const style = OptionIndex(p[PT.CAPTION_STYLE], CaptionStyle.Count);
    const cap = CaptionSizeFromParam(p[PT.CAPTION_SIZE]) * height;
    const scale = cap / FONT.kCapHeight;
    const em = FONT.kEm * scale;
    const gap = CaptionSpacingFromParam(p[PT.CAPTION_SPACING]) * em;
    out.scale = f32(scale);

    const lines = [Printable(bytes).subarray(0, kMaxCaption)];
    if (style === CaptionStyle.Legend) {
      // A catalogue number that moves on with every print.
      const a = String((1013 + steps) % 10000).padStart(4, '0');
      const b = String(pcg(steps >>> 0) % 100).padStart(2, '0');
      lines.push(encoder.encode(`NO. ${a}-${b}`));
    }

    let total = 0;
    for (const line of lines) total += line.length;
    const show = clamp(p[PT.CAPTION_SHOW], 0.0, 1.0);
    const head = show * (total + kRevealLetters);

    let x0 = 1e30;
    let y0 = 1e30;
    let x1 = -1e30;
    let y1 = -1e30;
    let index = 0;
    for (let k = 0; k < lines.length; k += 1) {
      const line = lines[k];
      let widthOf = 0.0;
      for (const ch of line) widthOf += AdvanceOf(ch) * em + gap;
      if (line.length > 0) widthOf -= gap;

      let pen = style === CaptionStyle.Legend ? width * 0.94 - widthOf : 0.5 * (width - widthOf);
      // Caption Y is where the first line's capitals are centred. The first
      // product is float times int in the C++, so a float.
      const baseline = f32(p[PT.CAPTION_Y] * height) - 0.5 * cap - k * 1.9 * cap;
      for (const ch of line) {
        let code = ch;
        if (code < FONT.kFirst || code > 126) code = 63; // '?'
        const reveal = clamp((head - index) / kRevealLetters, 0.0, 1.0);
        if (code !== 32 && reveal > 0.0 && out.glyphs.length < kMaxGlyphs) {
          const g = {
            code,
            left: f32(pen - FONT.kPenX * scale),
            top: f32(baseline + FONT.kBaseline * scale),
            reveal: f32(reveal),
          };
          out.glyphs.push(g);
          x0 = min2(x0, g.left);
          x1 = max2(x1, g.left + FONT.kCellWidth * scale);
          y1 = max2(y1, g.top);
          y0 = min2(y0, g.top - FONT.kCellHeight * scale);
        }
        pen += AdvanceOf(code) * em + gap;
        index += 1;
      }
    }
    if (out.glyphs.length > 0) out.box = [f32(x0), f32(y0), f32(x1), f32(y1)];
    return out;
  }

  /**
   * ProcessOpenGL's CPU side for a `width` x `height` input. Everything the
   * passes need, in the order ProcessOpenGL decides it; no GL.
   */
  Plan(width, height) {
    const p = this.params;
    const state = this.state;

    //---------------------------------------------------------------------
    // Time and the beat.
    //---------------------------------------------------------------------
    this.UpdateClock();
    const dt = this.lastNow >= 0.0 ? clamp(this.now - this.lastNow, 0.0, kMaxFrameDelta) : 0.0;
    this.lastNow = this.now;
    this.elapsed += dt;

    if (this.lastBarPhase >= 0.0 && this.barPhase !== this.lastBarPhase) {
      this.hostBeatSeen = true;
      if (this.barPhase < f32(this.lastBarPhase - 0.5)) this.bars += 1;
    }
    this.lastBarPhase = this.barPhase;

    // ensureBuffers: every buffer follows the raster, so a held print of
    // another size (or none) means everything is about to be new and empty.
    state.reallocated = !(this.heldWidth > 0) || this.heldWidth !== width || this.heldHeight !== height;
    this.heldWidth = width;
    this.heldHeight = height;
    this.glowWidth = max2(1, Math.trunc((width + 3) / 4));
    this.glowHeight = max2(1, Math.trunc((height + 3) / 4));
    const fresh = state.reallocated;

    this.UpdateSteps(dt);
    state.steps = this.stepCount;

    const aspect = width / height;
    const film = Math.floor(this.elapsed * kFilmRate);

    //---------------------------------------------------------------------
    // The freeze. A press freezes what is on screen; with a timed hold a
    // press while frozen hits again, on the clip as it is now (what is on
    // screen then is the freeze itself), and latched it lets go.
    //---------------------------------------------------------------------
    const hold = FreezeHoldFromParam(p[PT.FREEZE_HOLD]);
    let freezeCapture = false;
    let rehit = false;
    if (this.freezePresses > 0) {
      if (this.freezing && hold === 0.0) {
        this.freezing = false;
      } else {
        rehit = this.freezing;
        this.freezing = true;
        this.freezeAge = 0.0;
        freezeCapture = true;
      }
      this.freezePresses = 0;
    } else if (this.freezing) {
      this.freezeAge += dt;
      if (hold > 0.0 && this.freezeAge >= hold) this.freezing = false;
    }
    const fadeTime = FreezeFadeFromParam(p[PT.FREEZE_FADE]);
    state.frozen = this.freezing;
    state.grey = this.freezing ? (fadeTime > 0.0 ? clamp(this.freezeAge / fadeTime, 0.0, 1.0) : 1.0) : 0.0;
    state.flash = this.freezing ? clamp(p[PT.FREEZE_FLASH], 0.0, 1.0) * Math.exp(-this.freezeAge / kFlashDecay) : 0.0;
    state.push = this.freezing
      ? 1.0 + FreezePushFromParam(p[PT.FREEZE_PUSH]) * (1.0 - Math.exp(-this.freezeAge / kPushSettle))
      : 1.0;

    //---------------------------------------------------------------------
    // 1. The prints: which copies the capture pass makes, before the print
    //    pass. A fresh set of buffers starts from this frame.
    //---------------------------------------------------------------------
    const weave = WeaveFromParam(p[PT.WEAVE]);
    const copies = [];
    if (fresh) {
      copies.push({ into: 'held', index: 0, from: 'source', overscan: 1.0, offset: [0.0, 0.0] });
      copies.push({ into: 'held', index: 1, from: 'source', overscan: 1.0, offset: [0.0, 0.0] });
      copies.push({ into: 'frozen', from: 'source', overscan: 1.0, offset: [0.0, 0.0] });
    } else {
      if (freezeCapture && rehit) copies.push({ into: 'frozen', from: 'source', overscan: 1.0, offset: [0.0, 0.0] });
      else if (freezeCapture) copies.push({ into: 'frozen', from: 'trail', index: this.current, overscan: 1.0, offset: [0.0, 0.0] });
      if (state.captured) {
        // A re-photographed print never sits where the last one did; the
        // overscan keeps its edge out of the frame however far it lands.
        this.newest = 1 - this.newest;
        const reach = 1.0 / (1.0 - 2.0 * weave);
        const ox = (weave * signedHash(this.stepCount, 1)) / aspect;
        const oy = weave * signedHash(this.stepCount, 2);
        copies.push({ into: 'held', index: this.newest, from: 'source', overscan: f32(reach), offset: [f32(ox), f32(oy)] });
      }
    }

    const dissolve = clamp(p[PT.DISSOLVE], 0.0, 1.0);
    const into = this.stepPosition - Math.floor(this.stepPosition);
    state.fade = dissolve > 0.0 ? clamp(into / dissolve, 0.0, 1.0) : 1.0;
    const halfLife = TrailHalfLifeFromParam(p[PT.TRAIL]);
    state.decay = fresh || halfLife <= 0.0 ? 0.0 : Math.pow(0.5, dt / halfLife);

    const previous = this.current;
    this.current = 1 - this.current;
    const print = {
      newest: this.newest, previous, current: this.current,
      Fade: f32(state.fade),
      Stutter: clamp(p[PT.STUTTER], 0.0, 1.0),
      FreezeMix: this.freezing ? 1.0 : 0.0,
      Decay: f32(state.decay),
    };

    //---------------------------------------------------------------------
    // 2. The geometry. Freeze Push about the frame's centre, then the
    //    enlargement about its target, as one affine map; then the stretch.
    //---------------------------------------------------------------------
    const generations = clamp(lround(p[PT.GENERATIONS]), 1, 8);
    const run = OptionIndex(p[PT.ENLARGE_RUN], EnlargeRun.Count);
    const deepest = MagnifyFromParam(p[PT.MAGNIFY]);
    let cycle = 0;
    let depth = 0.0;
    if (run === EnlargeRun.Cycle) {
      cycle = Math.trunc(this.stepCount / (generations + 1));
      depth = this.stepCount % (generations + 1);
    } else {
      depth = clamp(p[PT.ENLARGE], 0.0, 1.0) * generations;
    }
    const magnifyAt = (g) => Math.pow(deepest, g / generations);
    const m = magnifyAt(depth);
    state.generation = depth;
    state.magnify = m;

    // Each enlargement is a new print, a hair off the last: the offsets add
    // up, each in the units of the generation that made it.
    let shiftX = 0.0;
    let shiftY = 0.0;
    for (let j = 1; j <= generations; j += 1) {
      const w = clamp(depth - (j - 1), 0.0, 1.0);
      if (w <= 0.0) break;
      shiftX += (w * 0.004 * signedHash(cycle * 16 + j, 3)) / magnifyAt(j);
      shiftY += (w * 0.004 * signedHash(cycle * 16 + j, 4)) / magnifyAt(j);
    }
    // And the film shimmers in the gate a little every frame.
    shiftX += 0.12 * weave * signedHash(film, 5);
    shiftY += 0.12 * weave * signedHash(film, 6);

    const targetX = p[PT.ENLARGE_X] * aspect;
    const targetY = p[PT.ENLARGE_Y];
    const push = state.push;
    state.scale = 1.0 / (push * m);
    state.biasX = (0.5 * aspect * (1.0 - 1.0 / push)) / m + targetX * (1.0 - 1.0 / m) + shiftX;
    state.biasY = (0.5 * (1.0 - 1.0 / push)) / m + targetY * (1.0 - 1.0 / m) + shiftY;

    const stretchGain = StretchFromParam(p[PT.STRETCH]) - 1.0;
    const clock = this.elapsed % 1000.0;
    const magnified = 1.0 / state.scale;
    const smooth = clamp((magnified - 1.0) / 0.5, 0.0, 1.0);
    const frame = {
      Size: [width, height],
      Aspect: f32(aspect),
      Scale: f32(state.scale),
      Bias: [f32(state.biasX), f32(state.biasY)],
      Smooth: f32(smooth * smooth * (3.0 - 2.0 * smooth)),
      StretchCentre: [f32(p[PT.STRETCH_X] * aspect), p[PT.STRETCH_Y]],
      StretchGain: f32(stretchGain),
      StretchWidth: f32(StretchWidthFromParam(p[PT.STRETCH_WIDTH])),
      Writhe: clamp(p[PT.WRITHE], 0.0, 1.0),
      Clock: f32(clock),
    };

    //---------------------------------------------------------------------
    // 3. The aura's mask and its blur, at a quarter of the size.
    //---------------------------------------------------------------------
    const auraGain = f32(3.0 * clamp(p[PT.AURA], 0.0, 1.0));
    const keyMode = OptionIndex(p[PT.AURA_KEY], AuraKey.Count);
    const threshold = clamp(p[PT.AURA_THRESHOLD], 0.0, 1.0);
    const sigma = f32(max2(0.5, (AuraSizeFromParam(p[PT.AURA_SIZE]) * height) / 4.0));
    const aura = { on: auraGain > 0.0, Lod: 2.0, KeyMode: keyMode, Threshold: threshold, Sigma: sigma };

    //---------------------------------------------------------------------
    // The film: flicker per film frame, the torch's path, the grain layers.
    //---------------------------------------------------------------------
    const flicker = clamp(p[PT.FLICKER], 0.0, 1.0);
    let exposure = 1.0 + flicker * 0.16 * signedHash(film, 7);
    if (hash01(film >>> 0, 8) < 0.05 * flicker) exposure -= 0.3 * flicker; // a frame the lamp nearly lost
    state.exposure = exposure;

    const sweep = TorchSweepSecondsFromParam(p[PT.TORCH_SWEEP]);
    const radius = TorchSizeFromParam(p[PT.TORCH_SIZE]);
    let torchX = p[PT.TORCH_X] * aspect;
    let torchY = p[PT.TORCH_Y];
    if (sweep > 0.0) {
      this.torchPhase = (this.torchPhase + dt / sweep) % 1.0;
      const angle = (TorchAngleFromParam(p[PT.TORCH_ANGLE]) * kPi) / 180.0;
      const reach = 0.5 * Math.sqrt(aspect * aspect + 1.0) + 2.0 * radius;
      const along = (2.0 * this.torchPhase - 1.0) * reach;
      torchX += Math.cos(angle) * along;
      torchY += Math.sin(angle) * along;
    }
    state.torchX = torchX;
    state.torchY = torchY;

    const grainCell = (GrainSizeFromParam(p[PT.GRAIN_SIZE]) * height) / 1080.0;
    const layerScale = new Float32Array(kMaxLayers);
    const layerWeight = new Float32Array(kMaxLayers);
    const layerShift = new Float32Array(kMaxLayers * 2);
    const layerSeed = new Int32Array(kMaxLayers);
    let layers = 0;
    // The grain each earlier generation left in its print, frozen there and
    // enlarged with the picture ever since...
    for (let j = 0; j < generations && layers < kMaxLayers - 1; j += 1) {
      const w = clamp(depth - j, 0.0, 1.0);
      if (w <= 0.0) break;
      layerScale[layers] = (grainCell * m) / magnifyAt(j);
      layerWeight[layers] = w;
      layerSeed[layers] = pcg((cycle * 16 + j) >>> 0) & 0x7fffffff;
      layerShift[layers * 2] = 64.0 * hash01(j >>> 0, 9);
      layerShift[layers * 2 + 1] = 64.0 * hash01(j >>> 0, 10);
      layers += 1;
    }
    // ...and the live camera's, new every film frame. Last, always.
    layerScale[layers] = grainCell;
    layerWeight[layers] = 1.0;
    layerSeed[layers] = pcg(((film >>> 0) ^ 0x5bd1e995) >>> 0) & 0x7fffffff;
    layers += 1;
    state.layerCount = layers;
    state.layerScale = layerScale.slice();
    state.layerWeight = layerWeight.slice();

    // The point the whole map holds still, in output pixels: the grain is
    // anchored there, so a frozen layer grows about it with the picture.
    let originX = targetX;
    let originY = targetY;
    if (state.scale < 0.9999) {
      originX = state.biasX / (1.0 - state.scale);
      originY = state.biasY / (1.0 - state.scale);
    }

    this.layout = this.LayOutCaption(width, height, this.captionText, this.stepCount);
    const layout = this.layout;
    const glyphs = new Float32Array(kMaxGlyphs * 4);
    layout.glyphs.forEach((g, i) => {
      glyphs[i * 4 + 0] = g.code;
      glyphs[i * 4 + 1] = g.left;
      glyphs[i * 4 + 2] = g.top;
      glyphs[i * 4 + 3] = g.reveal;
    });

    //---------------------------------------------------------------------
    // 4. The composite, into the host's framebuffer.
    //---------------------------------------------------------------------
    const tint = OptionIndex(p[PT.TINT], Tint.Count);
    const duo = kTints[tint];
    const auraColour = kAuraColours[OptionIndex(p[PT.AURA_COLOUR], AuraColour.Count)];
    const composite = {
      Size: [width, height],
      Aspect: f32(aspect),
      Clock: f32(clock),
      ContrastAmount: clamp(p[PT.CONTRAST], 0.0, 1.0),
      TintDark: duo.dark,
      TintLight: duo.light,
      TintAmount: tint === Tint.Natural ? 0.0 : clamp(p[PT.TINT_AMOUNT], 0.0, 1.0),
      KeyMode: keyMode,
      Threshold: threshold,
      Silhouette: clamp(p[PT.SILHOUETTE], 0.0, 1.0),
      AuraGain: auraGain,
      AuraColour: auraColour,
      Corona: clamp(p[PT.CORONA], 0.0, 1.0),
      GlowTexel: [f32(1.0 / this.glowWidth), f32(1.0 / this.glowHeight)],
      GlowSigma: sigma,
      TorchAmount: clamp(p[PT.TORCH], 0.0, 1.0),
      TorchCentre: [f32(torchX), f32(torchY)],
      TorchRadius: f32(radius),
      Exposure: f32(exposure),
      Vignette: f32(f32(0.85) * clamp(p[PT.VIGNETTE], 0.0, 1.0)), // 0.85f: a float times a float
      GrainAmount: f32(f32(0.35) * clamp(p[PT.GRAIN], 0.0, 1.0)),
      LayerCount: layers,
      LayerScale: layerScale,
      LayerWeight: layerWeight,
      LayerShift: layerShift,
      LayerSeed: layerSeed,
      GrainOrigin: [f32((originX / aspect) * width), f32(originY * height)],
      GreyAmount: f32(state.grey),
      Flash: f32(state.flash),
      GlyphCount: layout.glyphs.length,
      Glyphs: glyphs,
      CaptionBox: layout.box,
      CaptionScale: layout.scale,
      // Half the field's change across one output pixel: one pixel wide at any size.
      CaptionEdge: max2(f32(0.002), f32(1.0 / f32(4.0 * FONT.kSpread * layout.scale))),
      CaptionGlow: clamp(p[PT.CAPTION_GLOW], 0.0, 1.0),
      CaptionLevel: f32(0.97 * (0.5 + 0.5 * exposure)),
      MixAmount: clamp(p[PT.MIX], 0.0, 1.0),
    };

    return {
      width, height, glowWidth: this.glowWidth, glowHeight: this.glowHeight,
      fresh, copies, print, frame, aura, composite,
    };
  }
}

/** The atlas's bytes, from fontatlas.js's base64. */
function atlasBytes() {
  const raw = atob(FONT.kAtlasBase64);
  const bytes = new Uint8Array(raw.length);
  for (let i = 0; i < raw.length; i += 1) bytes[i] = raw.charCodeAt(i);
  return bytes;
}

/** InitGL's atlas: R8, linear, clamped, and nothing left bound. */
function makeAtlas(gl) {
  const texture = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D, texture);
  gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1);
  gl.texStorage2D(gl.TEXTURE_2D, 1, gl.R8, FONT.kAtlasWidth, FONT.kAtlasHeight);
  gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, FONT.kAtlasWidth, FONT.kAtlasHeight, gl.RED, gl.UNSIGNED_BYTE, atlasBytes());
  gl.pixelStorei(gl.UNPACK_ALIGNMENT, 4);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
  gl.bindTexture(gl.TEXTURE_2D, null);
  return texture;
}

/** GLState.h's unbindTextureUnits. */
function unbindTextureUnits(gl, count) {
  for (let unit = count - 1; unit >= 0; unit -= 1) bindTexture(gl, unit, null);
}

/** The plugin's buffers and passes. */
class BluebookGL {
  constructor(gl, quad, programs) {
    this.gl = gl;
    this.quad = quad;
    this.programs = programs;
    this.held = [new PassBuffer(gl), new PassBuffer(gl)]; ///< the two newest prints
    this.trail = [new PassBuffer(gl), new PassBuffer(gl)]; ///< the print stage's result, this frame's and last
    this.frozen = new PassBuffer(gl); ///< what was on screen when Freeze was pressed
    this.framed = new PassBuffer(gl, { mip: true }); ///< the geometry's result, mipmapped for the mask
    this.mask = new PassBuffer(gl); ///< quarter size
    this.blurA = new PassBuffer(gl);
    this.blurB = new PassBuffer(gl);
    this.atlas = makeAtlas(gl);
  }

  /** ensureBuffers: everything follows the raster. The trail is full float (AGENTS.md, trap 2). */
  ensureBuffers(width, height, glowWidth, glowHeight) {
    const gl = this.gl;
    for (const b of this.held) b.ensure(width, height, gl.RGBA16F);
    for (const b of this.trail) b.ensure(width, height, gl.RGBA32F);
    this.frozen.ensure(width, height, gl.RGBA16F);
    this.framed.ensure(width, height, gl.RGBA16F);
    this.mask.ensure(glowWidth, glowHeight, gl.R16F);
    this.blurA.ensure(glowWidth, glowHeight, gl.R16F);
    this.blurB.ensure(glowWidth, glowHeight, gl.R16F);
  }

  /** copyInto: the capture pass. */
  copyInto(target, texture, maxU, maxV, width, height, overscan, offsetX, offsetY) {
    const gl = this.gl;
    const shader = this.programs.capture;
    target.bind();
    shader.use();
    bindTexture(gl, 0, texture);
    shader.setSampler('InputTexture', 0);
    shader.set('MaxUV', maxU, maxV);
    shader.set('Texel', f32(1.0 / width), f32(1.0 / height));
    shader.set('Overscan', overscan);
    shader.set('Offset', offsetX, offsetY);
    shader.setInt('Hooks', 0);
    this.quad.draw();
    unbindTextureUnits(gl, 1);
  }

  /** ProcessOpenGL's passes, for what Plan() decided, from `source` to the canvas. */
  draw(f, source) {
    const gl = this.gl;
    const { width, height } = f;
    gl.disable(gl.BLEND);
    this.ensureBuffers(width, height, f.glowWidth, f.glowHeight);

    // 1. The prints. The kit hands a texture exactly the raster's size, so
    //    GetMaxGLTexCoords' maxima are 1.
    for (const c of f.copies) {
      const target = c.into === 'frozen' ? this.frozen : this.held[c.index];
      const texture = c.from === 'trail' ? this.trail[c.index].texture : source;
      this.copyInto(target, texture, 1.0, 1.0, width, height, c.overscan, c.offset[0], c.offset[1]);
    }
    {
      const shader = this.programs.print;
      this.trail[f.print.current].bind();
      shader.use();
      bindTexture(gl, 0, source);
      bindTexture(gl, 1, this.held[f.print.newest].texture);
      bindTexture(gl, 2, this.held[1 - f.print.newest].texture);
      bindTexture(gl, 3, this.trail[f.print.previous].texture);
      bindTexture(gl, 4, this.frozen.texture);
      shader.setSampler('InputTexture', 0);
      shader.setSampler('Held', 1);
      shader.setSampler('HeldBefore', 2);
      shader.setSampler('TrailBefore', 3);
      shader.setSampler('Frozen', 4);
      shader.set('Fade', f.print.Fade);
      shader.set('Stutter', f.print.Stutter);
      shader.set('FreezeMix', f.print.FreezeMix);
      shader.set('Decay', f.print.Decay);
      shader.setInt('Hooks', 0);
      this.quad.draw();
      unbindTextureUnits(gl, 5);
    }

    // 2. The geometry.
    {
      const u = f.frame;
      const shader = this.programs.frame;
      this.framed.bind();
      shader.use();
      bindTexture(gl, 0, this.trail[f.print.current].texture);
      shader.setSampler('Picture', 0);
      shader.set('Size', u.Size[0], u.Size[1]);
      shader.set('Aspect', u.Aspect);
      shader.set('Scale', u.Scale);
      shader.set('Bias', u.Bias[0], u.Bias[1]);
      shader.set('Smooth', u.Smooth);
      shader.set('StretchCentre', u.StretchCentre[0], u.StretchCentre[1]);
      shader.set('StretchGain', u.StretchGain);
      shader.set('StretchWidth', u.StretchWidth);
      shader.set('Writhe', u.Writhe);
      shader.set('Clock', u.Clock);
      shader.setInt('Hooks', 0);
      this.quad.draw();
      unbindTextureUnits(gl, 1);
    }

    // 3. The aura's mask and its blur, at a quarter of the size.
    if (f.aura.on) {
      this.framed.generateMipmap();
      {
        const shader = this.programs.mask;
        this.mask.bind();
        shader.use();
        bindTexture(gl, 0, this.framed.texture);
        shader.setSampler('Picture', 0);
        shader.set('Lod', f.aura.Lod);
        shader.setInt('KeyMode', f.aura.KeyMode);
        shader.set('Threshold', f.aura.Threshold);
        shader.setInt('Hooks', 0);
        this.quad.draw();
        unbindTextureUnits(gl, 1);
      }
      const axes = [
        { from: this.mask, to: this.blurA, dx: f32(1.0 / f.glowWidth), dy: 0.0 },
        { from: this.blurA, to: this.blurB, dx: 0.0, dy: f32(1.0 / f.glowHeight) },
      ];
      for (const axis of axes) {
        const shader = this.programs.blur;
        axis.to.bind();
        shader.use();
        bindTexture(gl, 0, axis.from.texture);
        shader.setSampler('Mask', 0);
        shader.set('Direction', axis.dx, axis.dy);
        shader.set('Sigma', f.aura.Sigma);
        shader.setInt('Hooks', 0);
        this.quad.draw();
        unbindTextureUnits(gl, 1);
      }
    }

    // 4. The composite, into the host's framebuffer: here, the canvas.
    {
      const u = f.composite;
      const shader = this.programs.composite;
      gl.bindFramebuffer(gl.FRAMEBUFFER, null);
      gl.viewport(0, 0, width, height);
      shader.use();
      bindTexture(gl, 0, source);
      bindTexture(gl, 1, this.framed.texture);
      bindTexture(gl, 2, this.blurB.texture);
      bindTexture(gl, 3, this.atlas);
      shader.setSampler('InputTexture', 0);
      shader.setSampler('Framed', 1);
      shader.setSampler('Glow', 2);
      shader.setSampler('Atlas', 3);
      shader.set('Size', u.Size[0], u.Size[1]);
      shader.set('Aspect', u.Aspect);
      shader.set('Clock', u.Clock);
      shader.set('ContrastAmount', u.ContrastAmount);
      shader.set('TintDark', ...u.TintDark);
      shader.set('TintLight', ...u.TintLight);
      shader.set('TintAmount', u.TintAmount);
      shader.setInt('KeyMode', u.KeyMode);
      shader.set('Threshold', u.Threshold);
      shader.set('Silhouette', u.Silhouette);
      shader.set('AuraGain', u.AuraGain);
      shader.set('AuraColour', ...u.AuraColour);
      shader.set('Corona', u.Corona);
      shader.set('GlowTexel', u.GlowTexel[0], u.GlowTexel[1]);
      shader.set('GlowSigma', u.GlowSigma);
      shader.set('TorchAmount', u.TorchAmount);
      shader.set('TorchCentre', u.TorchCentre[0], u.TorchCentre[1]);
      shader.set('TorchRadius', u.TorchRadius);
      shader.set('Exposure', u.Exposure);
      shader.set('Vignette', u.Vignette);
      shader.set('GrainAmount', u.GrainAmount);
      shader.setInt('LayerCount', u.LayerCount);
      shader.setArray('LayerScale', u.LayerScale, 1);
      shader.setArray('LayerWeight', u.LayerWeight, 1);
      // vec2[] and int[]: the kit's setArray takes 1, 3 or 4 float components.
      const shift = shader.location('LayerShift[0]') ?? shader.location('LayerShift');
      if (shift !== null) gl.uniform2fv(shift, u.LayerShift); else shader.missing.add('LayerShift');
      const seed = shader.location('LayerSeed[0]') ?? shader.location('LayerSeed');
      if (seed !== null) gl.uniform1iv(seed, u.LayerSeed); else shader.missing.add('LayerSeed');
      shader.set('GrainOrigin', u.GrainOrigin[0], u.GrainOrigin[1]);
      shader.set('GreyAmount', u.GreyAmount);
      shader.set('Flash', u.Flash);
      shader.setInt('GlyphCount', u.GlyphCount);
      shader.setArray('Glyphs', u.Glyphs, 4);
      shader.set('CaptionBox', u.CaptionBox[0], u.CaptionBox[1], u.CaptionBox[2], u.CaptionBox[3]);
      shader.set('CaptionScale', u.CaptionScale);
      shader.set('CaptionEdge', u.CaptionEdge);
      shader.set('CaptionGlow', u.CaptionGlow);
      shader.set('CaptionLevel', u.CaptionLevel);
      shader.set('MixAmount', u.MixAmount);
      shader.setInt('Hooks', 0);
      this.quad.draw();
      unbindTextureUnits(gl, 4);
    }
  }
}

// ==== END FRAME ============================================================

//===========================================================================
// The transport: the page's BPM clock, standing in for Resolume's.
//===========================================================================

const BPM_LOW = 20;
const BPM_HIGH = 300;
const transport = {
  bpm: 120,
  bars: 0, ///< bars since the top, accumulated from the page's seconds
  last: null, ///< the page's seconds at the last frame
  /** The bar phase at the page's `seconds`: advanced at the BPM, four beats to the bar. */
  advance(seconds) {
    if (this.last === null || seconds < this.last) this.bars = 0; // a start, or Restart
    else this.bars += ((seconds - this.last) * this.bpm) / 240;
    this.last = seconds;
    return this.bars - Math.floor(this.bars);
  },
};

//===========================================================================
// The parameters: the constructor's, in ParamId order and groups, with the
// plugin's names and defaults. [page id, ParamId, readout, hint].
//===========================================================================

const fixed = (n, d) => n.toFixed(d);
const sig = (n) => (n >= 100 ? n.toFixed(0) : n >= 10 ? n.toFixed(1) : n >= 1 ? n.toFixed(2) : n.toFixed(3));
const V = (v) => f32(v);
const pct = (fraction) => `${sig(fraction * 100)}% of the height`;
const plain = (v) => fixed(V(v), 2);
const GENERATION_NAMES = ['1', '2', '3', '4', '5', '6', '7', '8'];

const SPEC = [
  ['stutter', PT.STUTTER, plain, 'The live clip (0) against the step-printed one (1).'],
  ['stepOn', PT.STEP_ON, null, 'What takes a print. Rate: Step Rate prints a second. Beat, 1/2 Beat, 1/4 Beat and Bar follow the transport: on this page the Host BPM clock under the picture, in Resolume the composition’s.'],
  ['stepRate', PT.STEP_RATE, (v) => `${sig(StepRateFromParam(V(v)))} a second`, 'Prints a second at Step On Rate, 1 to 30.'],
  ['dissolve', PT.DISSOLVE, (v) => `${fixed(100 * V(v), 0)}% of a print`, 'How much of each print is spent dissolving in from the last. 0 cuts.'],
  ['weave', PT.WEAVE, (v) => `${fixed(100 * WeaveFromParam(V(v)), 2)}% of the height`, 'How far each print sits off true, up to 1.2% of the frame height each way, and a shimmer of a tenth of that every film frame.'],
  ['trail', PT.TRAIL, (v) => (TrailHalfLifeFromParam(V(v)) > 0 ? `${sig(TrailHalfLifeFromParam(V(v)))} s half-life` : 'off'), 'A smear of the result into itself, with a half-life of up to 1.5 s.'],

  ['enlarge', PT.ENLARGE, plain, 'How many generations in, as a share of Generations. Each generation is an enlargement of the last print, with its own grain.'],
  ['enlargeRun', PT.ENLARGE_RUN, null, 'Manual: Enlarge says how far in. Cycle: one generation further in on every print, then back out.'],
  ['magnify', PT.MAGNIFY, (v) => `×${sig(MagnifyFromParam(V(v)))}`, 'The last generation’s magnification, 1.5× to 24×.'],
  ['generations', PT.GENERATIONS, null, 'How many enlargements it takes to get to Magnify. FF_TYPE_INTEGER, 1 to 8, in the plugin; a dropdown here.'],
  ['enlargeX', PT.ENLARGE_X, plain, 'What it zooms towards, across the frame. FF_TYPE_XPOS in the plugin.'],
  ['enlargeY', PT.ENLARGE_Y, plain, 'What it zooms towards, up the frame. FF_TYPE_YPOS in the plugin.'],

  ['stretch', PT.STRETCH, (v) => `×${fixed(StretchFromParam(V(v)), 2)}`, 'The face pulled long: 1× to 5× down the centre of the column.'],
  ['stretchWidth', PT.STRETCH_WIDTH, (v) => pct(StretchWidthFromParam(V(v))), 'How wide the pull is, 4% to 80% of the frame height.'],
  ['writhe', PT.WRITHE, plain, 'How much the pull wobbles and sways.'],
  ['stretchX', PT.STRETCH_X, plain, 'The column’s centre, across the frame. FF_TYPE_XPOS in the plugin.'],
  ['stretchY', PT.STRETCH_Y, plain, 'The column’s centre, up the frame. FF_TYPE_YPOS in the plugin.'],

  ['aura', PT.AURA, plain, 'The glowing hand: a glow round the subject, not on it.'],
  ['auraSize', PT.AURA_SIZE, (v) => pct(AuraSizeFromParam(V(v))), 'The glow’s reach (its standard deviation), 0.3% to 12% of the frame height.'],
  ['auraKey', PT.AURA_KEY, null, 'What the subject is: brighter than Aura Threshold, darker than it (a hand against a light box), or the clip’s alpha.'],
  ['auraThreshold', PT.AURA_THRESHOLD, plain, 'Where the key cuts.'],
  ['auraColour', PT.AURA_COLOUR, null, 'The glow’s colour.'],
  ['corona', PT.CORONA, plain, 'Streamers running outwards from the subject’s edges.'],
  ['silhouette', PT.SILHOUETTE, plain, 'Darkens the subject itself, for the black shape against the light.'],

  ['torch', PT.TORCH, plain, 'A dark room with a beam crossing it, lit in linear light.'],
  ['torchSize', PT.TORCH_SIZE, (v) => pct(TorchSizeFromParam(V(v))), 'The beam’s radius, 4% to 80% of the frame height.'],
  ['torchSweep', PT.TORCH_SWEEP, (v) => (TorchSweepSecondsFromParam(V(v)) > 0 ? `${sig(TorchSweepSecondsFromParam(V(v)))} s a pass` : 'still'), 'Seconds per pass across the frame, 20 s to 0.5 s; at 0 the beam stands still at Torch X and Y.'],
  ['torchAngle', PT.TORCH_ANGLE, (v) => `${fixed(TorchAngleFromParam(V(v)), 0)}°`, 'The direction the beam travels: 0° left to right, 90° bottom to top.'],
  ['torchX', PT.TORCH_X, plain, 'Where the beam is, or the middle of its path. FF_TYPE_XPOS in the plugin.'],
  ['torchY', PT.TORCH_Y, plain, 'Where the beam is, or the middle of its path. FF_TYPE_YPOS in the plugin.'],

  ['tint', PT.TINT, null, 'The duotone: shadows to light along the luma. Natural keeps the clip’s own colour and grades it only by Contrast.'],
  ['tintAmount', PT.TINT_AMOUNT, plain, 'How much of the duotone.'],
  ['contrast', PT.CONTRAST, plain, 'Towards a smoothstep, which crushes both ends.'],
  ['grain', PT.GRAIN, plain, 'Film grain, strongest in the mid-tones.'],
  ['grainSize', PT.GRAIN_SIZE, (v) => `${fixed(GrainSizeFromParam(V(v)), 2)} px at 1080`, 'The grain’s cell, 1 to 4 pixels at 1080 lines, and in proportion at any other size.'],
  ['flicker', PT.FLICKER, plain, 'The exposure, frame by frame on a 24 fps film, and now and then a frame the lamp nearly lost.'],
  ['vignette', PT.VIGNETTE, plain, 'The dark edge.'],

  ['caption', PT.CAPTION, null, 'Up to 48 characters. Anything outside plain ASCII is drawn as one ?.'],
  ['captionShow', PT.CAPTION_SHOW, plain, '0 hidden, 1 every letter in; between, revealed from the left a letter at a time.'],
  ['captionStyle', PT.CAPTION_STYLE, null, 'Title: centred. Legend: at the right, with a catalogue number that moves on with every print.'],
  ['captionSize', PT.CAPTION_SIZE, (v) => pct(CaptionSizeFromParam(V(v))), 'The capital height, 1.5% to 15% of the frame height.'],
  ['captionSpacing', PT.CAPTION_SPACING, (v) => `${fixed(CaptionSpacingFromParam(V(v)), 2)} em`, 'Letter spacing added to every advance, 0 to 0.6 em.'],
  ['captionGlow', PT.CAPTION_GLOW, plain, 'The halo round the letters.'],
  ['captionY', PT.CAPTION_Y, plain, 'Where the first line’s capitals are centred, up the frame. FF_TYPE_YPOS in the plugin.'],

  ['freeze', PT.FREEZE, null, 'The freeze-frame hit: the frame on screen freezes with a flash, drains to black and white and pushes in. FF_TYPE_EVENT in the plugin; here a toggle the page lets go of after one frame.'],
  ['freezeHold', PT.FREEZE_HOLD, (v) => (FreezeHoldFromParam(V(v)) > 0 ? `${sig(FreezeHoldFromParam(V(v)))} s` : 'latched'), 'How long a freeze lasts, 0.25 to 10 s. At the top it latches until Freeze is pressed again.'],
  ['freezeFade', PT.FREEZE_FADE, (v) => (FreezeFadeFromParam(V(v)) > 0 ? `${fixed(FreezeFadeFromParam(V(v)), 2)} s` : 'cut'), 'Colour to grey, 0 to 1.5 s. 0 cuts.'],
  ['freezeFlash', PT.FREEZE_FLASH, plain, 'The flash of the still being taken.'],
  ['freezePush', PT.FREEZE_PUSH, (v) => `${fixed(100 * FreezePushFromParam(V(v)), 1)}%`, 'How far in the camera pushes over the hold, 0 to 20%.'],

  ['mix', PT.MIX, plain, 'The treated picture against the clip. At 0 the clip comes back as it went in.'],
];

const PARAMS = SPEC.map(([id, slot, display, hint]) => {
  const info = INFO[slot];
  const p = { id, slot, name: info.name, group: info.group, hint };
  switch (info.kind) {
    case Kind.Option:
      Object.assign(p, { type: 'option', elements: info.options, default: info.defaultValue });
      break;
    case Kind.Integer:
      Object.assign(p, { type: 'option', elements: GENERATION_NAMES, default: lround(info.defaultValue) - info.minimum });
      break;
    case Kind.Text:
      Object.assign(p, { type: 'text', default: info.text, placeholder: kDefaultCaption });
      break;
    case Kind.Event:
      Object.assign(p, { type: 'boolean', default: 0 });
      break;
    default:
      Object.assign(p, { type: 'standard', display, default: info.defaultValue });
  }
  return p;
});
if (PARAMS.length !== PT.ABOUT_FIRST || PARAMS.some((p, i) => p.slot !== i)) {
  throw new Error('the page declares every control, in ParamId order');
}

/** What a host would send for a page value: the integer for Generations, the float otherwise. */
function toHost(p, value) {
  return INFO[p.slot].kind === Kind.Integer ? INFO[p.slot].minimum + Math.round(value) : value;
}

//===========================================================================
// The renderer: one plugin instance, driven as a host drives it.
//===========================================================================

function createRenderer(gl, quad) {
  // The trail is RGBA32F and is read through texture() with LINEAR filtering
  // (by the frame pass, and by the capture pass when Freeze copies it), which
  // WebGL2 allows a float texture only with this. Without it the texture is
  // incomplete, every read is zero and the picture would be black or frozen
  // on nothing: a wrong picture, so the page stops instead.
  if (!gl.getExtension('OES_texture_float_linear')) {
    throw new GLError('This browser can’t filter a 32-bit float texture (OES_texture_float_linear), and the plugin reads its smear buffer that way. Without it every frame would read as black, so the demo stops here.');
  }

  const vertex = S.VERSION + S.QUAD_VERTEX;
  const assemble = (piece) => S.VERSION + S.CONSTANTS + S.COMMON + piece;
  const programs = {
    capture: new Program(gl, vertex, assemble(S.CAPTURE_FRAGMENT), 'capture'),
    print: new Program(gl, vertex, assemble(S.PRINT_FRAGMENT), 'print'),
    frame: new Program(gl, vertex, assemble(S.FRAME_FRAGMENT), 'frame'),
    mask: new Program(gl, vertex, assemble(S.MASK_FRAGMENT), 'mask'),
    blur: new Program(gl, vertex, assemble(S.BLUR_FRAGMENT), 'blur'),
    composite: new Program(gl, vertex, assemble(S.COMPOSITE_FRAGMENT), 'composite'),
  };

  const plugin = new BluebookPlugin();
  const passes = new BluebookGL(gl, quad, programs);
  let lastText = null;
  let frames = 0;
  let fpsAt = performance.now();
  let fpsFrames = 0;
  let fps = 0;
  let stats = null;

  return {
    plugin,
    programs,
    get stats() { return stats; },

    render({ input, params, time }) {
      // What the host sets: the page's controls, as the floats a host sends.
      for (const p of PARAMS) {
        if (p.type === 'text') {
          const value = params.get(p.id);
          if (value !== lastText) {
            plugin.SetTextParameter(p.slot, value);
            lastText = value;
          }
        } else {
          plugin.SetFloatParameter(p.slot, toHost(p, params.get(p.id)));
        }
      }
      const pressed = params.get('freeze') > 0.5;
      plugin.SetTime(time);
      plugin.SetBeatInfo(transport.bpm, transport.advance(time));

      // The plugin sizes everything from its input, as from the host's texture.
      const planned = plugin.Plan(input.width, input.height);
      passes.draw(planned, input.texture);

      // A host lets go of an event; the kit has none, so the page does.
      if (pressed) params.set('freeze', 0);

      frames += 1;
      fpsFrames += 1;
      const wall = performance.now();
      if (wall - fpsAt >= 1000) {
        fps = (fpsFrames * 1000) / (wall - fpsAt);
        fpsAt = wall;
        fpsFrames = 0;
      }
      const s = plugin.state;
      stats = {
        frames,
        fps,
        prints: s.steps,
        fade: s.fade,
        generation: s.generation,
        magnify: s.magnify,
        frozen: s.frozen,
        freezeAge: plugin.freezeAge,
        hold: FreezeHoldFromParam(plugin.params[PT.FREEZE_HOLD]),
        stepOn: OptionIndex(plugin.params[PT.STEP_ON], StepOn.Count),
        width: planned.width,
        height: planned.height,
      };
    },
  };
}

//===========================================================================
// The page.
//===========================================================================

const DEMO = {
  name: 'Bluebook',
  pluginId: 'BK01',
  kind: 'effect',
  tagline:
    'The look of a 1990s paranormal TV title sequence, and a crime drama’s freeze-frame hit, as stages that stack: the clip step-printed with a dissolve and a tired gate’s weave, enlarged generation by generation until it is mostly grain, a face pulled long, a glow round the bright thing with streamers running off it, a torch in a dark room, a cold teal grade with grain and flicker, and a caption spelling itself out. Freeze holds the frame on screen with a flash, drains it to black and white and pushes in.',
  repo: 'https://github.com/stoatworks-labs/bluebook',

  blurb:
    'It’s Bluebook’s own GLSL, all six passes, ported from the repository to WebGL2 unedited, on the plugin’s own buffers and its own caption font, copied in as data. A JavaScript port of the plugin’s CPU half (the print clock, the freeze, the enlargement’s generations, the torch, the caption’s layout) feeds it, held to the plugin’s harness by a check in the repository. It runs on generated clips, or your own, and the beat is a clock the page keeps.',

  needFloat: true,
  showBackdrop: true,

  // The scene is the general case; lights on black give the Aura its subject
  // (the README's glowing hand is a bright thing on dark); the geometry card
  // makes the enlargement, the stretch and the weave readable; the shape on
  // transparency is what Aura Key Alpha keys; the bars and ramps show the grade.
  sources: ['scene', 'spot', 'grid', 'alpha', 'bars', 'ramp', 'detail'],

  params: PARAMS,

  differences: [
    'The CPU half is a port. The plugin decides on the CPU, in double: every control’s mapping (Controls.cpp); when a print is taken, from Step Rate or the host’s bar phase, and how far it dissolves and smears; where each print sits off true; the freeze’s hold, grey, flash and push; how many generations in the enlargement is, each one’s mis-registration and its grain; the exposure’s flicker; the torch’s path; and where each letter of the caption goes (Bluebook.cpp). Here all of that is JavaScript, whose numbers are doubles too, with every control rounded to a float on the way in, as the host’s are. demo/tools/check_port.mjs drives this port and the plugin’s own harness (bbtest --trace) through the same frames and compares what each decided, frame by frame: the print count, the dissolve, the trail’s decay, the freeze, the generation and magnification, the affine map, the torch, the exposure, the grain layers and every glyph of the caption.',
    'The GPU half isn’t a port: the capture, print, frame, mask, blur and composite passes are the plugin’s own GLSL, on the same half- and full-float buffers, and so is the caption’s font, a signed distance field copied in byte for byte. demo/tools/check_shaders.py fails the repository’s verify script if a character of the shaders, the way they’re joined, a number of the font or a byte of its atlas drifts. WebGL2 needs EXT_color_buffer_float to render into those buffers and OES_texture_float_linear to filter the full-float one, and the page stops and says so without either.',
    'The tempo is simulated. Resolume hands the plugin its BPM and bar phase every frame; this page keeps a BPM clock of its own (the Host BPM field under the picture, 120 at first) and hands the plugin that BPM and a bar phase advanced from the page’s own seconds, as the plugin’s harness does. Step On’s Beat, 1/2 Beat, 1/4 Beat and Bar print on it. There’s no tap tempo and no music for it to follow. Pause stops it with the clock, and Restart puts it back to the top of a bar.',
    'The clock is the page’s: seconds from frame deltas, capped at a tenth of a second a frame (the plugin caps its own at a quarter). The plugin’s vote on whether the host sends seconds or milliseconds isn’t ported, because this host’s unit is known, as the harness’s is. The first frame has no time in it, as in the plugin. Restart takes the clock backwards, which the plugin treats as no time passing, so the prints and a freeze carry on as they were. Step is one frame of a sixtieth of a second.',
    'Freeze is FF_TYPE_EVENT in the plugin; here it’s a toggle the page lets go of after one frame, one rising edge, which is what the plugin counts as a press. Generations is FF_TYPE_INTEGER, 1 to 8; the kit has no integer control, so it’s a dropdown of the same eight values. Enlarge X and Y, Stretch X and Y, Torch X and Y and Caption Y are FF_TYPE_XPOS and FF_TYPE_YPOS; here they’re plain sliders. The About block isn’t on the panel.',
    'The caption goes to the plugin as the UTF-8 bytes of what you type, which is an assumption about what a host sends: nobody has typed a caption into Resolume yet. Each character outside printable ASCII is one question mark, and it keeps the first 48 characters.',
    'The clip is the kit’s generated one or your own file, 8-bit and premultiplied, as Resolume hands an effect its input. The README’s picture was made with the plugin’s harness on its own test card (a figure with a raised hand under a night sky), which isn’t one of these clips. Changing the Composition size is the host’s raster changing under a running instance: every buffer is reallocated, so the prints, a freeze and the smear start again from that frame, as they do in the plugin.',
    'Nothing here is measured. The plugin’s harness checks that prints land on the frame and the beat the model says, the smear rises as its half-life to a ULP, an enlargement magnifies by what it says to a quarter of a pixel, the aura falls off as a blurred edge does, the caption’s capitals are the height asked for to the pixel and a freeze holds the frame bit for bit, with 13 deliberately wrong models and 17 mutants caught. That harness, not this page, is the reason to believe the picture. The plugin has never been loaded into Resolume on macOS; on Windows it has run in Arena, on software rendering.',
  ],

  createRenderer: (gl, quad) => {
    renderer = createRenderer(gl, quad);
    return renderer;
  },
};

let renderer = null;
const mounted = typeof window === 'undefined' ? null : mountDemo(DEMO);

//---------------------------------------------------------------------------
// The Host BPM field and the status line.
//---------------------------------------------------------------------------
if (mounted) {
  const params = mounted.params;
  const query = new URLSearchParams(window.location.search);
  const embed = query.has('embed') && query.get('embed') !== '0';
  const queryBpm = Number(query.get('bpm'));
  if (Number.isFinite(queryBpm) && queryBpm >= BPM_LOW && queryBpm <= BPM_HIGH) transport.bpm = queryBpm;

  // Copy link carries the BPM, since it is not a parameter.
  const toQuery = params.toQuery.bind(params);
  params.toQuery = () => {
    const q = toQuery();
    if (transport.bpm !== 120) q.set('bpm', String(transport.bpm));
    return q;
  };

  if (!embed) {
    const bar = document.querySelector('.transport');
    if (bar) {
      const label = document.createElement('label');
      label.className = 'transport__field';
      const span = document.createElement('span');
      span.className = 'transport__label';
      span.textContent = 'Host BPM';
      const bpm = document.createElement('input');
      bpm.type = 'number';
      bpm.className = 'prow__text';
      bpm.min = String(BPM_LOW);
      bpm.max = String(BPM_HIGH);
      bpm.step = '1';
      bpm.value = String(transport.bpm);
      bpm.inputMode = 'numeric';
      bpm.style.width = '6em';
      bpm.style.flex = '0 0 auto';
      bpm.id = 'bluebook-bpm';
      label.title = 'A simulated transport: this BPM, and a bar phase from this page’s clock, handed to the plugin every frame as Resolume hands over the composition’s. Step On’s Beat, 1/2 Beat, 1/4 Beat and Bar print on it.';
      bpm.addEventListener('change', () => {
        const v = Number(bpm.value);
        if (Number.isFinite(v) && v >= BPM_LOW && v <= BPM_HIGH) {
          transport.bpm = v;
          if (!mounted.state.playing) mounted.redraw();
        } else {
          bpm.value = String(transport.bpm);
        }
      });
      label.append(span, bpm);
      bar.append(label);
    }

    // A status line: what the port decided this frame. It reports; it measures nothing.
    const stage = document.querySelector('.stage');
    if (stage) {
      const line = document.createElement('p');
      line.className = 'stage__status';
      line.id = 'bluebook-stats';
      line.setAttribute('aria-live', 'off');
      stage.append(line);
      setInterval(() => {
        const s = renderer?.stats;
        if (!s) return;
        const bars = transport.bars;
        const beat = Math.floor((bars - Math.floor(bars)) * 4) + 1;
        const clock = s.stepOn === StepOn.Rate ? 'Step Rate' : `${transport.bpm} BPM, bar ${Math.floor(bars) + 1} beat ${beat}`;
        const freeze = s.frozen ? ` · frozen ${fixed(s.freezeAge, 1)} s${s.hold > 0 ? ` of ${sig(s.hold)}` : ', latched'}` : '';
        const enlarge = s.generation > 0 ? ` · ${fixed(s.generation, 2)} generations in, ×${sig(s.magnify)}` : '';
        line.textContent =
          `print ${s.prints} (${clock}) · dissolved ${fixed(100 * s.fade, 0)}%${enlarge}${freeze}`
          + ` · ${s.width} × ${s.height} · ${fixed(s.fps, 0)} frames a second`;
      }, 250);
    }
  }

  // For headless checks: the page's pieces, read-only by convention.
  window.bluebookDemo = { mounted, transport, PARAMS, PT, get renderer() { return renderer; } };
}

export {
  BluebookPlugin, INFO, PT, Kind, PARAMS, SPEC, OptionIndex, AdvanceOf, pcg, hash01, signedHash,
  StepRateFromParam, ParamFromStepRate, WeaveFromParam, TrailHalfLifeFromParam, MagnifyFromParam, ParamFromMagnify,
  StretchFromParam, StretchWidthFromParam, AuraSizeFromParam, ParamFromAuraSize, TorchSizeFromParam, ParamFromTorchSize,
  TorchSweepSecondsFromParam, TorchAngleFromParam, GrainSizeFromParam, CaptionSizeFromParam, ParamFromCaptionSize,
  CaptionSpacingFromParam, FreezeHoldFromParam, FreezeFadeFromParam, FreezePushFromParam,
};
