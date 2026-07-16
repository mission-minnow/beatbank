/*
 * canvas.js — Beat Bank Pattern View (Schwung canvas overlay).
 *
 * Genre-first browsing of the (large) library:
 *   - jog turn         -> cycle patterns WITHIN the current genre
 *   - Shift + jog turn -> switch genre (needs ctx.shiftHeld(); falls back to
 *                         plain pattern-cycle on a host without it)
 *   - Knobs 1-6 turn   -> change each voice's pad (drumrack) / note (gm), live
 *   - Knob 7 turn      -> swing (0..100)
 *   - Knob 8 turn      -> switch genre (jumps to that genre's first pattern)
 *   - jog click / Back -> exit (host closes the canvas; the pick is already set)
 *
 * Everything is on the jog + knobs — encoders the shadow chain UI owns. The
 * nav arrows (Move eats them for its sequencer) and Shift do NOT reach a
 * chain-preview canvas, so pad editing is POSITIONAL rather than banked:
 *   K1 edits the 1st voice row, K2 the 2nd, .. up to K6 (6th row).
 * Voices appear in canonical order (kick, snare, ch, oh, then extras), so a
 * given voice keeps its pad across patterns even though which knob reaches it
 * depends on what the current pattern plays. Pads are the frequent edit, so
 * they get K1-K6; swing/genre sit on K7/K8 (rarely touched once dialed in).
 * Each row shows its knob number + the pad it drives; changes persist in state.
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
const CC_PAD_LO = 71, CC_PAD_HI = 76;  /* K1..K6 = positional pad/note selectors */
const CC_SWING = 77;                    /* K7 = swing */
const CC_GENRE = 78;                    /* K8 = genre */
const SWING_STEP = 5;

/* K8 genre selector is deliberately "geared down": Move's encoders are endless,
 * so we spend more wheel travel per genre to make the (~24-genre) list less
 * twitchy. It takes this many detents to advance one genre — at ~24 detents per
 * physical revolution that's ~8 genres per full turn. Bump it up/down to taste
 * after feeling it on hardware (higher = coarser, more spins to cross the list). */
const GENRE_DETENTS_PER_STEP = 3;
let genreAccum = 0;

const g = {
  count: 1, pattern: 0, steps: 16, name: '', genre: '', swing: 0,
  drumrack: true,
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

/* Voices the current pattern plays, in canonical order = the display rows.
 * Knobs are POSITIONAL: K3 edits row 0, K4 row 1, .. K8 row 5. A voice keeps
 * its pad across patterns (notes are per-instance) even though the knob that
 * reaches it depends on what the current pattern plays. */
function usedVoices() {
  const out = [];
  for (let v = 0; v < NUM_VOICES; v++) if (g.rows[v] && g.rows[v].length) out.push(v);
  return out;
}

function editNote(ctx, voice, dir) {
  if (voice === undefined || voice < 0) return;
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

  ctx.print(0, 0, (g.name || '').slice(0, 16), 1);   /* pattern name — full top row now */
  ctx.print(96, 0, pos + '/' + ge.count, 1);

  const used = usedVoices();

  const steps = g.steps;
  const gridX = 50, gridW = 76;   /* leaves room for label + pad + knob tag */
  const cellW = Math.max(2, Math.floor(gridW / steps));
  const topY = 8;
  const pitch = Math.min(9, Math.floor(48 / Math.max(1, used.length)));
  const cellH = Math.max(1, pitch - 3);

  for (let r = 0; r < used.length; r++) {
    const v = used[r], row = g.rows[v], y = topY + r * pitch;
    ctx.print(0, y, r < 6 ? String(r + 1) : '', 1);          /* 1.. knob number */
    ctx.print(10, y, VOICES[v].label, 1);                     /* KCK  */
    ctx.print(30, y, padLabel(g.note[v]), 1);                 /* p3   */
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

  ctx.print(0, 57, 'sw:' + g.swing + '  K8: ' + (ge.name || '').slice(0, 12), 1);
}

globalThis.canvas_overlay = {
  onOpen(ctx) { g.genres = []; genreAccum = 0; load(ctx, true); },
  tick(ctx) { load(ctx, false); },
  draw(ctx) { draw(ctx); return true; },
  onMidi(ctx, payload) {
    const d = payload && payload.data;
    if (!d || d.length < 3) return;
    const type = d[0] & 0xF0, b1 = d[1], b2 = d[2];
    if (type !== 0xB0 || b2 === 0) return;   /* encoders: 1..63 = +, 64..127 = - */
    const dir = b2 < 64 ? 1 : -1;
    if (b1 === CC_JOG) {   /* shift+jog = genre, plain jog = pattern (falls back if host lacks shiftHeld) */
      const shifted = typeof ctx.shiftHeld === 'function' && ctx.shiftHeld();
      (shifted ? switchGenre : cyclePattern)(ctx, dir);
      return;
    }
    if (b1 === CC_GENRE) {
      if (dir * genreAccum < 0) genreAccum = 0;   /* reversing drops stale travel */
      genreAccum += dir;
      while (genreAccum >= GENRE_DETENTS_PER_STEP)  { switchGenre(ctx,  1); genreAccum -= GENRE_DETENTS_PER_STEP; }
      while (genreAccum <= -GENRE_DETENTS_PER_STEP) { switchGenre(ctx, -1); genreAccum += GENRE_DETENTS_PER_STEP; }
      return;
    }
    if (b1 === CC_SWING) { setSwing(ctx, dir); return; }
    if (b1 >= CC_PAD_LO && b1 <= CC_PAD_HI) {
      editNote(ctx, usedVoices()[b1 - CC_PAD_LO], dir);   /* positional: K1 -> row 0 */
      return;
    }
  },
  onClose() {},
  onExit() {},
};
