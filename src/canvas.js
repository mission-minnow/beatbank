/*
 * canvas.js — Beat Bank Pattern View (Schwung canvas overlay).
 *
 * The only interaction is picking a beat, so this is a pure view:
 *   - jog turn  -> browse/select patterns (sets the pattern live)
 *   - jog click -> exit (host closes the canvas; the pattern is already set)
 *   - Back      -> exit
 *
 * It shows the step grid plus, per voice, the MIDI note and the drum-rack PAD
 * number it drives (pad = note - 35, for notes 36..51) — so you can see which
 * pad to swap in MrDrums to change, say, the kick sample.
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
const CC_JOG = 14;

const g = {
  count: 1, pattern: 0, steps: 16, name: '', genre: '',
  rows: new Array(NUM_VOICES).fill(''),
  note: new Array(NUM_VOICES).fill(0),
  rev: -1,
};

function gp(ctx, k) { const v = ctx.getParam(k); return v === undefined || v === null ? '' : v; }
function gpi(ctx, k, d) { const v = parseInt(gp(ctx, k), 10); return Number.isFinite(v) ? v : d; }

function load(ctx, force) {
  const rev = gpi(ctx, 'preview_rev', 0);
  const notesOnly = !force && rev === g.rev;
  if (!notesOnly) {
    g.rev = rev;
    g.count = Math.max(1, gpi(ctx, 'pattern_count', 1));
    g.pattern = gpi(ctx, 'pattern', 0);
    g.steps = Math.max(1, Math.min(32, gpi(ctx, 'steps', 16)));
    g.name = gp(ctx, 'pattern_name');
    g.genre = gp(ctx, 'pattern_genre');
    for (let v = 0; v < NUM_VOICES; v++) g.rows[v] = gp(ctx, 'row' + v);
  }
  /* notes can change via the Note Map toggle without a pattern change */
  for (let v = 0; v < NUM_VOICES; v++) g.note[v] = gpi(ctx, VOICES[v].key, g.note[v]);
}

function selectPattern(ctx, delta) {
  const n = g.count;
  const next = (((g.pattern + delta) % n) + n) % n;
  if (next === g.pattern) return;
  g.pattern = next;
  ctx.setParam('pattern', String(next));
  load(ctx, true);
}

function padLabel(note) {
  if (note >= 36 && note <= 51) return 'p' + (note - 35);  /* MrDrums pad number */
  return String(note);                                     /* out of pad range */
}

function draw(ctx) {
  ctx.print(0, 0, (g.genre || '').slice(0, 7), 1);
  ctx.print(40, 0, (g.name || '').slice(0, 9), 1);
  ctx.print(98, 0, (g.pattern + 1) + '/' + g.count, 1);

  const used = [];
  for (let v = 0; v < NUM_VOICES; v++) if (g.rows[v] && g.rows[v].length) used.push(v);

  const steps = g.steps;
  const gridX = 44, gridW = 82;
  const cellW = Math.max(2, Math.floor(gridW / steps));
  const topY = 8;
  /* Row pitch >= 8px keeps a blank line between the 7px-tall labels so p3/p7
   * etc. stay readable even with rows above and below. */
  const pitch = Math.min(10, Math.floor(48 / Math.max(1, used.length)));
  const cellH = Math.max(1, pitch - 3);

  for (let r = 0; r < used.length; r++) {
    const v = used[r], row = g.rows[v], y = topY + r * pitch;
    ctx.print(0, y, VOICES[v].label, 1);
    ctx.print(20, y, padLabel(g.note[v]), 1);   /* note/pad the drum drives */
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

  ctx.print(0, 57, 'jog:beat   click/back:exit', 1);
}

globalThis.canvas_overlay = {
  onOpen(ctx) { load(ctx, true); },
  tick(ctx) { load(ctx, false); },
  draw(ctx) { draw(ctx); return true; },
  onMidi(ctx, payload) {
    const d = payload && payload.data;
    if (!d || d.length < 2) return;
    if ((d[0] & 0xF0) === 0xB0 && d[1] === CC_JOG) {
      const b2 = d.length > 2 ? d[2] : 0;
      selectPattern(ctx, b2 > 0 && b2 < 64 ? 1 : -1);
    }
  },
  onClose() {},
  onExit() {},
};
