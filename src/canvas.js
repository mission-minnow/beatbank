/*
 * canvas.js — Beat Bank Pattern View (Schwung canvas overlay).
 *
 * Genre-first browsing of the (large) library:
 *   - jog turn         -> cycle patterns WITHIN the current genre
 *   - Knob 1 turn      -> switch genre (jumps to that genre's first pattern)
 *   - Knob 2 turn      -> swing (0..100)
 *   - Knobs 3-8 turn   -> change each voice's pad (drumrack) / note (gm), live
 *   - Shift + Knobs    -> second bank for the extra voices (busy patterns)
 *   - jog click / Back -> exit (host closes the canvas; the pick is already set)
 *
 * Everything is on the jog + knobs (encoders the shadow chain UI owns) and
 * Shift (CC 49, also forwarded to the overlay) — NOT the nav arrows, which
 * Move's firmware consumes for its own sequencer before a chain-preview canvas
 * ever sees them.
 *
 * Knob -> voice map (per pattern; only voices the pattern plays are editable):
 *   K3=Kick K4=Snare K5=CH K6=OH   (the fixed main 4)
 *   K7,K8   = 1st & 2nd extra voice
 *   Shift+K3,K4,K5.. = extra voices 3,4,5.. (row tag shows "s3","s4",..)
 * Each row shows the drum-rack PAD (pad = note - 35, notes 36..51) or raw note,
 * plus the knob tag that edits it. Pad changes persist in the slot's state.
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
const MAIN = [0, 1, 2, 3];          /* kick, snare, ch, oh — always on K3..K6 */

const CC_JOG = 14, CC_SHIFT = 49;
const CC_KNOB1 = 71, CC_KNOB2 = 72;  /* K1=genre  K2=swing */
const CC_KNOB3 = 73, CC_KNOB8 = 78;  /* K3..K8 = pad/note selectors */
const SWING_STEP = 5;

const g = {
  count: 1, pattern: 0, steps: 16, name: '', genre: '', swing: 0,
  drumrack: true, shift: false,
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
  g.swing = gpi(ctx, 'swing', g.swing);
  g.drumrack = gp(ctx, 'note_map') !== 'gm';
  for (let v = 0; v < NUM_VOICES; v++) g.note[v] = gpi(ctx, VOICES[v].key, g.note[v]);
}

/* Non-main voices the current pattern actually plays, in canonical order. */
function presentExtras() {
  const out = [];
  for (let v = 4; v < NUM_VOICES; v++) if (g.rows[v] && g.rows[v].length) out.push(v);
  return out;
}

/* Physical knob (3..8) + shift -> voice index, or -1 if that knob is unused. */
function knobVoice(knob, shift) {
  if (!shift) {
    if (knob >= 3 && knob <= 6) return MAIN[knob - 3];
    const ex = presentExtras();
    return ex[knob - 7] === undefined ? -1 : ex[knob - 7];   /* K7,K8 -> extra 0,1 */
  }
  const ex = presentExtras();
  const idx = (knob - 3) + 2;                                 /* Shift+K3 -> extra 2 */
  return ex[idx] === undefined ? -1 : ex[idx];
}

/* Reverse: knob tag shown on a voice's row ("3".."8", or "s3".. under Shift). */
function knobTag(voice) {
  const m = MAIN.indexOf(voice);
  if (m >= 0) return String(m + 3);
  const ei = presentExtras().indexOf(voice);
  if (ei === 0) return '7';
  if (ei === 1) return '8';
  if (ei >= 2 && ei <= 7) return 's' + ((ei - 2) + 3);
  return '';
}

function editNote(ctx, voice, dir) {
  if (voice < 0) return;
  const lo = g.drumrack ? 36 : 0, hi = g.drumrack ? 51 : 127;
  const n = Math.max(lo, Math.min(hi, g.note[voice] + dir));
  if (n === g.note[voice]) return;
  g.note[voice] = n;
  ctx.setParam(VOICES[voice].key, String(n));
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

function setSwing(ctx, delta) {
  const s = Math.max(0, Math.min(100, g.swing + delta * SWING_STEP));
  if (s === g.swing) return;
  g.swing = s;
  ctx.setParam('swing', String(s));
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
  ctx.print(44, 0, (g.name || '').slice(0, 8), 1);
  ctx.print(96, 0, pos + '/' + ge.count, 1);
  if (g.shift) ctx.print(122, 0, '^', 1);

  const used = [];
  for (let v = 0; v < NUM_VOICES; v++) if (g.rows[v] && g.rows[v].length) used.push(v);

  const steps = g.steps;
  const gridX = 50, gridW = 76;   /* leaves room for label + pad + knob tag */
  const cellW = Math.max(2, Math.floor(gridW / steps));
  const topY = 8;
  const pitch = Math.min(9, Math.floor(48 / Math.max(1, used.length)));
  const cellH = Math.max(1, pitch - 3);

  for (let r = 0; r < used.length; r++) {
    const v = used[r], row = g.rows[v], y = topY + r * pitch;
    ctx.print(0, y, VOICES[v].label, 1);           /* KCK  */
    ctx.print(20, y, padLabel(g.note[v]), 1);       /* p3   */
    ctx.print(38, y, knobTag(v), 1);                /* 3 / 7 / s3 */
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

  ctx.print(0, 57, g.shift ? 'SHIFT: extra-voice pads'
                           : 'jog K1:gen K2:sw:' + g.swing + ' K3+:pad', 1);
}

globalThis.canvas_overlay = {
  onOpen(ctx) { g.genres = []; load(ctx, true); },
  tick(ctx) { load(ctx, false); },
  draw(ctx) { draw(ctx); return true; },
  onMidi(ctx, payload) {
    const d = payload && payload.data;
    if (!d || d.length < 3) return;
    const type = d[0] & 0xF0, b1 = d[1], b2 = d[2];
    if (type !== 0xB0) return;
    if (b1 === CC_SHIFT) { g.shift = b2 > 0; return; }   /* momentary bank modifier */
    if (b2 === 0) return;                                 /* encoders: 1..63 +, 64..127 - */
    const dir = b2 < 64 ? 1 : -1;
    if (b1 === CC_JOG)   { cyclePattern(ctx, dir); return; }
    if (b1 === CC_KNOB1) { switchGenre(ctx, dir); return; }
    if (b1 === CC_KNOB2) { setSwing(ctx, dir); return; }
    if (b1 >= CC_KNOB3 && b1 <= CC_KNOB8) {
      editNote(ctx, knobVoice(b1 - 70, g.shift), dir);   /* b1-70 = knob number 3..8 */
      return;
    }
  },
  onClose() {},
  onExit() {},
};
