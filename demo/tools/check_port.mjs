#!/usr/bin/env node
/**
 * check_port.mjs -- hold the browser demo's CPU port to the plugin's own.
 *
 *   node demo/tools/check_port.mjs [--bbtest PATH] [--verbose]
 *
 * demo/plugin.js ports the CPU half of Bluebook.cpp: the print clock, the
 * dissolve and the trail's decay, the weave, the freeze, the enlargement's
 * generations, the affine map, the flicker, the torch's sweep, the grain
 * layers and the caption's layout. `bbtest --trace N` runs the REAL plugin
 * class through ProcessOpenGL, as a host does, and prints what its CPU half
 * decided every frame (Bluebook.h's FrameState and the CaptionLayout). This
 * drives the port through the same frames, on the same clock, beat and cues
 * as bbtest's Rig (Rig::Render, prepare(), applySetting()), and compares the
 * two line by line:
 *
 *   F frame steps captured fade decay frozen grey flash push generation
 *     magnify scale biasX biasY torchX torchY exposure layers
 *     layerScale... layerWeight...
 *   C frame glyphs scale x0 y0 x1 y1 [code left top reveal]...
 *
 * Integers (the print count, whether a print was taken, the freeze, the
 * number of grain layers and glyphs, each glyph's code) must be equal. The
 * doubles are printed with %.17g, so they round-trip, and the floats with
 * %.9g, which round-trips a float; they must agree to 1e-9 of their size (or
 * of 1, whichever is larger). The port computes in double as the plugin does,
 * but V8's pow, exp, log and cos are not the Mac's libm, and may differ in the
 * last bit; this reports how close it actually came.
 *
 * It needs a bbtest with --trace (cmake --build <dir> --target bbtest). It
 * looks for build-demo/bbtest, then build/bbtest, then build-verify/bbtest.
 * Exit status: 0 agreed, 1 a difference, 2 could not run.
 */

import { spawnSync } from 'node:child_process';
import { existsSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const repo = resolve(here, '..', '..');
const { BluebookPlugin, INFO, Kind } = await import(resolve(repo, 'demo', 'plugin.js'));

const args = process.argv.slice(2);
const verbose = args.includes('--verbose');
let bbtest = null;
const at = args.indexOf('--bbtest');
if (at >= 0) bbtest = resolve(args[at + 1] ?? '');
else bbtest = ['build-demo', 'build', 'build-verify'].map((d) => resolve(repo, d, 'bbtest')).find((p) => existsSync(p)) ?? null;
if (!bbtest || !existsSync(bbtest)) {
  console.error('check_port: no bbtest found; build one (cmake --build build --target bbtest) or pass --bbtest PATH');
  process.exit(2);
}

//---------------------------------------------------------------------------
// The scenarios. Each is bbtest's own command line: --set before the first
// frame, --at FRAME on that frame (an event's "=1" let go on the next).
//---------------------------------------------------------------------------

const SCENARIOS = [
  { name: 'defaults: Step On Rate, 60 fps', frames: 240 },
  { name: 'defaults at 50 fps, odd raster', frames: 200, fps: 50, size: [1001, 557] },
  { name: 'Step On Beat, 120 BPM', frames: 240, set: ['Step On=Beat'] },
  { name: 'Step On 1/4 Beat, 128 BPM, Dissolve 0', frames: 240, bpm: 128, set: ['Step On=1/4 Beat', 'Dissolve=0'] },
  { name: 'Step On Bar, 90 BPM', frames: 400, bpm: 90, set: ['Step On=Bar'] },
  { name: 'Rate to Beat mid-run, Step Rate moved', frames: 240, at: [[60, 'Step Rate=0.9'], [120, 'Step On=1/2 Beat'], [180, 'Step On=Rate']] },
  { name: 'no beat sent (bpm 0), Step On Beat', frames: 200, bpm: 0, set: ['Step On=Beat'] },
  { name: 'Enlarge Cycle, 5 generations, Weave 1', frames: 300, set: ['Enlarge Run=Cycle', 'Generations=5', 'Weave=1', 'Step Rate=0.7', 'Magnify=0.8', 'Enlarge X=0.7', 'Enlarge Y=0.35'] },
  { name: 'Enlarge 0.63 by hand, Enlarge X 0.3, 8 generations', frames: 120, set: ['Enlarge=0.63', 'Enlarge X=0.3', 'Generations=8', 'Grain Size=0.9'] },
  { name: 'Trail 0.6, Stutter 0.4', frames: 120, set: ['Trail=0.6', 'Stutter=0.4'] },
  { name: 'Freeze timed, pressed again while frozen', frames: 260, set: ['Freeze Hold=0.3', 'Freeze Push=1', 'Freeze Fade=0.5'], at: [[30, 'Freeze=1'], [70, 'Freeze=1']] },
  { name: 'Freeze latched, let go by a second press', frames: 200, set: ['Freeze Hold=1', 'Freeze Fade=0', 'Enlarge=0.4'], at: [[30, 'Freeze=1'], [90, 'Freeze=1']] },
  { name: 'Torch sweeping at an angle', frames: 300, size: [1920, 1080], set: ['Torch=1', 'Torch Sweep=0.85', 'Torch Angle=0.3', 'Torch Size=0.7', 'Torch X=0.4'] },
  { name: 'Torch Sweep 0 stands still', frames: 30, set: ['Torch=1', 'Torch Sweep=0'] },
  { name: 'Flicker 1, 600 frames', frames: 600, set: ['Flicker=1'] },
  { name: 'Caption Title, Show stepping, 1080p', frames: 120, size: [1920, 1080], set: ['Caption Spacing=0.6'], at: [[10, 'Caption Show=0.25'], [40, 'Caption Show=0.7'], [80, 'Caption Show=1'], [100, 'Caption Size=1']] },
  { name: 'Caption Legend, non-ASCII, low', frames: 120, caption: 'CAFÉ NOIR — X-7', set: ['Caption Style=Legend', 'Caption Show=1', 'Caption Y=0.2', 'Caption Size=0.2', 'Caption Spacing=0'] },
  { name: 'Caption over 48 bytes, Legend, revealing', frames: 90, caption: 'THE TRUTH IS NOT IN HERE, IT IS SOMEWHERE ELSE ENTIRELY', set: ['Caption Style=Legend', 'Caption Show=0.8'] },
  // DEL (0x7f) is the one byte AdvanceOf and the drawing disagree on inside kFirst..kFirst+kCount.
  { name: 'Caption with a DEL byte, odd raster, odd Caption Y', frames: 30, size: [1001, 557], caption: 'A\x7fB CD', set: ['Caption Show=1', 'Caption Y=0.731', 'Caption Size=0.37'] },
];

//---------------------------------------------------------------------------
// The port, driven as bbtest's Rig drives the plugin.
//---------------------------------------------------------------------------

const byName = new Map(INFO.map((info) => [info.name, info]));

/** applySetting(): an option by element name, otherwise a number (strtof's float), Caption as text. */
function applySetting(plugin, assignment) {
  const equals = assignment.indexOf('=');
  const name = assignment.slice(0, equals);
  const value = assignment.slice(equals + 1);
  const info = byName.get(name);
  if (!info) throw new Error(`no parameter called '${name}'`);
  if (info.kind === Kind.Text) return plugin.SetTextParameter(info.id, value);
  if (info.kind === Kind.Option) {
    const e = info.options.indexOf(value);
    if (e >= 0) return plugin.SetFloatParameter(info.id, e);
  }
  if (value.trim() === '' || !Number.isFinite(Number(value))) throw new Error(`'${value}' is not a number`);
  return plugin.SetFloatParameter(info.id, Math.fround(Number(value)));
}

const g9 = (v) => Number(Math.fround(v).toPrecision(9));

function runPort(s) {
  const [width, height] = s.size ?? [640, 360];
  const fps = s.fps ?? 60;
  const bpm = s.bpm ?? 120;
  const plugin = new BluebookPlugin();
  for (const setting of s.settings) applySetting(plugin, setting);
  const lines = [];
  for (let f = 0; f < s.frames; f += 1) {
    for (const [when, assignment] of s.at ?? []) {
      if (when === f) applySetting(plugin, assignment);
      else if (when + 1 === f && assignment.includes('=1')) {
        const info = byName.get(assignment.slice(0, assignment.indexOf('=')));
        if (info && info.kind === Kind.Event) plugin.SetFloatParameter(info.id, 0);
      }
    }
    const seconds = f / fps;
    plugin.SetTime(seconds);
    if (bpm > 0) plugin.SetBeatInfo(Math.fround(bpm), Math.fround(((seconds * bpm) / 240.0) % 1.0));
    plugin.Plan(width, height);
    const st = plugin.state;
    const n = st.layerCount;
    lines.push({
      F: [f, st.steps, st.captured ? 1 : 0, st.fade, st.decay, st.frozen ? 1 : 0, st.grey, st.flash, st.push, st.generation,
        st.magnify, st.scale, st.biasX, st.biasY, st.torchX, st.torchY, st.exposure, n,
        ...Array.from(st.layerScale.subarray(0, n)), ...Array.from(st.layerWeight.subarray(0, n))],
      C: [f, plugin.layout.glyphs.length, plugin.layout.scale, ...plugin.layout.box,
        ...plugin.layout.glyphs.flatMap((g) => [g.code, g.left, g.top, g.reveal])],
    });
  }
  return lines;
}

//---------------------------------------------------------------------------
// The plugin, through bbtest.
//---------------------------------------------------------------------------

function runPlugin(s) {
  const [width, height] = s.size ?? [640, 360];
  const argv = ['--trace', String(s.frames), '--size', `${width}x${height}`, '--fps', String(s.fps ?? 60), '--bpm', String(s.bpm ?? 120)];
  for (const setting of s.set ?? []) argv.push('--set', setting);
  for (const [when, assignment] of s.at ?? []) argv.push('--at', String(when), assignment);
  if (s.caption !== undefined) argv.push('--caption', s.caption);
  const run = spawnSync(bbtest, argv, { encoding: 'utf8', maxBuffer: 1 << 28 });
  if (run.status !== 0) {
    const why = `${run.stderr || ''}${run.stdout?.slice(0, 200) || ''}`.trim();
    if (/what is '--trace'/.test(why)) throw new Error(`${bbtest} has no --trace mode (tools/bbtest/main.cpp runTrace)`);
    throw new Error(`bbtest exited ${run.status}: ${why}`);
  }
  const lines = [];
  for (const raw of run.stdout.split('\n')) {
    if (!raw) continue;
    const [tag, ...rest] = raw.trim().split(/\s+/);
    const values = rest.map(Number);
    if (tag === 'F') lines.push({ F: values, C: null });
    else if (tag === 'C') lines[lines.length - 1].C = values;
  }
  return lines;
}

//---------------------------------------------------------------------------
// The comparison.
//---------------------------------------------------------------------------

const F_NAMES = ['frame', 'steps', 'captured', 'fade', 'decay', 'frozen', 'grey', 'flash', 'push', 'generation', 'magnify',
  'scale', 'biasX', 'biasY', 'torchX', 'torchY', 'exposure', 'layers'];
const F_INTEGERS = new Set([0, 1, 2, 5, 17]);
const TOLERANCE = 1e-9;

function fieldsOfF(line) {
  const n = line[17];
  return line.map((_, i) => (i < 18 ? { name: F_NAMES[i], integer: F_INTEGERS.has(i), float: false }
    : { name: i < 18 + n ? `layerScale[${i - 18}]` : `layerWeight[${i - 18 - n}]`, integer: false, float: true }));
}
function fieldsOfC(line) {
  return line.map((_, i) => {
    if (i === 0) return { name: 'frame', integer: true };
    if (i === 1) return { name: 'glyphs', integer: true };
    if (i < 7) return { name: ['scale', 'box.x0', 'box.y0', 'box.x1', 'box.y1'][i - 2], float: true };
    const k = Math.floor((i - 7) / 4);
    const part = ['code', 'left', 'top', 'reveal'][(i - 7) % 4];
    return { name: `glyph[${k}].${part}`, integer: part === 'code', float: part !== 'code' };
  });
}

const totals = { compared: 0, exact: 0, worst: 0, worstWhere: '', integers: 0, frames: 0, glyphs: 0, prints: 0, frozenFrames: 0 };
let failures = 0;

for (const s of SCENARIOS) {
  s.settings = [...(s.set ?? []), ...(s.caption !== undefined ? [`Caption=${s.caption}`] : [])];
  let plugin;
  try {
    plugin = runPlugin(s);
  } catch (error) {
    console.error(`check_port: ${error.message}`);
    process.exit(2);
  }
  const port = runPort(s);
  const problems = [];
  if (plugin.length !== port.length) problems.push(`bbtest printed ${plugin.length} frames, the port ${port.length}`);
  let worst = 0;
  let exact = 0;
  let compared = 0;
  for (let f = 0; f < Math.min(plugin.length, port.length) && problems.length < 6; f += 1) {
    for (const tag of ['F', 'C']) {
      const a = plugin[f][tag];
      const b = port[f][tag];
      if (!a || a.length !== b.length) {
        problems.push(`frame ${f} ${tag}: bbtest has ${a?.length ?? 0} numbers, the port ${b.length}`
          + (a ? ` (bbtest ${a.slice(0, 3).join(' ')}..., port ${b.slice(0, 3).join(' ')}...)` : ''));
        break;
      }
      const fields = tag === 'F' ? fieldsOfF(a) : fieldsOfC(a);
      for (let i = 0; i < a.length; i += 1) {
        const field = fields[i];
        const want = a[i];
        const got = field.float ? g9(b[i]) : b[i];
        compared += 1;
        if (field.integer) {
          totals.integers += 1;
          if (want !== got) problems.push(`frame ${f} ${field.name}: plugin ${want}, port ${got}`);
          else exact += 1;
          continue;
        }
        const diff = Math.abs(want - got);
        if (diff === 0) exact += 1;
        const scaled = diff / Math.max(1, Math.abs(want));
        if (scaled > worst) worst = scaled;
        if (scaled > totals.worst) {
          totals.worst = scaled;
          totals.worstWhere = `${s.name}, frame ${f} ${field.name}: plugin ${want}, port ${got}`;
        }
        if (!(scaled <= TOLERANCE)) problems.push(`frame ${f} ${field.name}: plugin ${want}, port ${got} (off by ${diff.toExponential(2)})`);
      }
    }
  }
  const last = plugin[plugin.length - 1];
  totals.compared += compared;
  totals.exact += exact;
  totals.frames += plugin.length;
  totals.prints += last?.F[1] ?? 0;
  totals.glyphs += plugin.reduce((n, line) => n + (line.C?.[1] ?? 0), 0);
  totals.frozenFrames += plugin.reduce((n, line) => n + (line.F[5] ?? 0), 0);
  const summary = `${plugin.length} frames, ${last?.F[1] ?? 0} prints, ${compared} numbers, ${exact} identical, worst ${worst.toExponential(2)}`;
  if (problems.length) {
    failures += 1;
    console.log(`FAIL  ${s.name}: ${summary}`);
    for (const p of problems.slice(0, 6)) console.log(`        ${p}`);
  } else {
    console.log(`ok    ${s.name}: ${summary}`);
  }
}

console.log('');
console.log(`${SCENARIOS.length} scenarios, ${totals.frames} frames, ${totals.prints} prints, ${totals.frozenFrames} frozen frames, ${totals.glyphs} glyphs placed`);
console.log(`${totals.compared} numbers compared (${totals.integers} integers): ${totals.exact} identical to the last bit; worst ${totals.worst.toExponential(2)} of its size${totals.worstWhere ? ` (${totals.worstWhere})` : ''}`);
if (verbose) console.log(`bbtest: ${bbtest}`);
if (failures) {
  console.log(`\n${failures} scenario(s) where demo/plugin.js decides differently from the plugin`);
  process.exit(1);
}
console.log('\nthe demo\'s CPU port decides what the plugin decides, frame by frame');
