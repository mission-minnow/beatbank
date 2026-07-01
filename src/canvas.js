/*
 * canvas.js — Beat Bank Pattern View (Schwung canvas overlay).
 *
 * Genre-first browsing of the (large) library:
 *   - jog turn         -> cycle patterns WITHIN the current genre
 *   - Knob 1 turn      -> switch genre (jumps to that genre's first pattern)
 *   - jog click / Back -> exit (host closes the canvas; the pick is already set)
 *
 * Genre switching is on Knob 1 (an encoder the shadow chain UI owns), not the
 * nav arrows — Move's firmware consumes the arrows for its own sequencer, so
 * they never reach a chain-preview canvas.
 *
 * Shows the step grid plus, per voice, the MIDI note and the drum-rack PAD
 * number it drives (pad = note - 35, for notes 36..51) so you can see which
 * pad to swap in your sampler.
 */

'use strict';

const VOICES = [
  { label: 'KCK', key: 'kick_note' },
  { label: 'SNR', key: 'snare_note' },
  { label: 'CH',  key: 'ch_note' },
  { label: 'OH',  key: 'oh_note' },
  { label: 'CLP', key: 'clap_note' },
  { label: 'RIM', key: 'rim_note' },
  { label: 'TOM', key: 'tom_note' },
  { label: 'RID', key: 'ride_note' },
  { label: 'CRS', key: 'crash_note' },
  { label: 'CWB', key: 'cowbell_note' },
  { label: 'CNG', key: 'conga_note' },
  { label: 'PRC', key: 'perc_note' },
];
const NUM_VOICES = VOICES.length;

const CC_JOG = 14, CC_KNOB1 = 71;   /* jog = pattern, knob 1 = genre */

const g = {
  count: 1, pattern: 0, steps: 16, name: '', genre: '',
  rows: new Array(NUM_VOICES).fill(''),
  note: new Array(NUM_VOICES).fill(0),
  genres: [],   /* [{name, start, count}] */
  rev: -1,
};

function gp(ctx, k) { const v = ctx.getParam(k); return v === undefined || v === null ? '' : v; }
function gpi(ctx, k, d) { const v = parseInt(gp(ctx, k), 10); return Number.isFinite(v) ? v : d; }

function parseGenres(str) {
  const arr = [];
  let start = 0;
  const parts = String(str || '').split('|');
  for (let i = 0; i < parts.length; i++) {
    const p = parts[i];
    const idx = p.lastIndexOf(':');
    if (idx < 0) continue;
    const name = p.slice(0, idx);
    const count = parseInt(p.slice(idx + 1), 10) || 0;
    if (count <= 0) continue;
    arr.push({ name: name, start: start, count: count });
    start += count;
  }
  return arr;
}

function genreIndexOf(p) {
  for (let i = 0; i < g.genres.length; i++) {
    const ge = g.genres[i];
    if (p >= ge.start && p < ge.start + ge.count) return i;
  }
  return 0;
}

function load(ctx, force) {
  const rev = gpi(ctx, 'preview_rev', 0);
  if (force || rev !== g.rev) {
    g.rev = rev;
    g.count = Math.max(1, gpi(ctx, 'pattern_count', 1));
    g.pattern = gpi(ctx, 'pattern', 0);
    g.steps = Math.max(1, Math.min(32, gpi(ctx, 'steps', 16)));
    g.name = gp(ctx, 'pattern_name');
    g.genre = gp(ctx, 'pattern_genre');
    for (let v = 0; v < NUM_VOICES; v++) g.rows[v] = gp(ctx, 'row' + v);
  }
  if (!g.genres.length) g.genres = parseGenres(gp(ctx, 'genre_list'));
  for (let v = 0; v < NUM_VOICES; v++) g.note[v] = gpi(ctx, VOICES[v].key, g.note[v]);
}

function setPattern(ctx, p) {
  if (p === g.pattern) return;
  g.pattern = p;
  ctx.setParam('pattern', String(p));
  load(ctx, true);
}

function cyclePattern(ctx, delta) {
  const ge = g.genres[genreIndexOf(g.pattern)];
  if (!ge) { setPattern(ctx, (((g.pattern + delta) % g.count) + g.count) % g.count); return; }
  const off = ((g.pattern - ge.start + delta) % ge.count + ge.count) % ge.count;
  setPattern(ctx, ge.start + off);
}

function switchGenre(ctx, delta) {
  const n = g.genres.length;
  if (!n) return;
  let gi = (genreIndexOf(g.pattern) + delta) % n;
  if (gi < 0) gi += n;
  setPattern(ctx, g.genres[gi].start);
}

function padLabel(note) {
  if (note >= 36 && note <= 51) return 'p' + (note - 35);
  return String(note);
}

function draw(ctx) {
  const gi = genreIndexOf(g.pattern);
  const ge = g.genres[gi] || { name: g.genre, start: 0, count: g.count };
  const pos = (g.pattern - ge.start) + 1;

  ctx.print(0, 0, (ge.name || '').slice(0, 7), 1);
  ctx.print(44, 0, (g.name || '').slice(0, 9), 1);
  ctx.print(102, 0, pos + '/' + ge.count, 1);

  const used = [];
  for (let v = 0; v < NUM_VOICES; v++) if (g.rows[v] && g.rows[v].length) used.push(v);

  const steps = g.steps;
  const gridX = 44, gridW = 82;
  const cellW = Math.max(2, Math.floor(gridW / steps));
  const topY = 8;
  const pitch = Math.min(9, Math.floor(48 / Math.max(1, used.length)));
  const cellH = Math.max(1, pitch - 3);

  for (let r = 0; r < used.length; r++) {
    const v = used[r], row = g.rows[v], y = topY + r * pitch;
    ctx.print(0, y, VOICES[v].label, 1);
    ctx.print(20, y, padLabel(g.note[v]), 1);
    for (let s = 0; s < steps && s < row.length; s++) {
      const c = row[s];
      if (c === '.') continue;
      const x = gridX + s * cellW, w = Math.max(1, cellW - 1);
      if (c === 'g') {
        ctx.fillRect(x, y + 2, w, Math.max(1, cellH - 2), 1);
      } else {
        ctx.fillRect(x, y + 1, w, cellH, 1);
        if (c === 'A') ctx.drawRect(x, y, w, 1, 1);
      }
    }
  }

  ctx.print(0, 57, 'jog:beat   K1:genre', 1);
}

globalThis.canvas_overlay = {
  onOpen(ctx) { g.genres = []; load(ctx, true); },
  tick(ctx) { load(ctx, false); },
  draw(ctx) { draw(ctx); return true; },
  onMidi(ctx, payload) {
    const d = payload && payload.data;
    if (!d || d.length < 3) return;
    const type = d[0] & 0xF0, b1 = d[1], b2 = d[2];
    if (type !== 0xB0 || b2 === 0) return;   /* encoders: 1..63 = +, 64..127 = - */
    const dir = b2 < 64 ? 1 : -1;
    if (b1 === CC_JOG)   { cyclePattern(ctx, dir); return; }
    if (b1 === CC_KNOB1) { switchGenre(ctx, dir); return; }
  },
  onClose() {},
  onExit() {},
};
