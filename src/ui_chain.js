/* Beat Bank's full-screen chain editor. Knob touches are delivered here by
 * Schwung, unlike a canvas embedded in the generic parameter editor. */
import { fontPrint4x5, fontWidth4x5 } from '/data/UserData/schwung/shared/param_pages/font4x5.mjs';
import { listKnobInit, listKnobStep } from '/data/UserData/schwung/shared/param_pages/list_knob.mjs';

const VOICES = [
  ['KCK', 'kick_note'], ['SNR', 'snare_note'], ['CH', 'ch_note'],
  ['OH', 'oh_note'], ['CLP', 'clap_note'], ['RIM', 'rim_note'],
  ['TOM', 'tom_note'], ['RID', 'ride_note'], ['CRS', 'crash_note'],
  ['CWB', 'cowbell_note'], ['CNG', 'conga_note'], ['PRC', 'perc_note'],
];
const g = { count: 1, pattern: 0, steps: 16, name: '', genre: '', swing: 0,
  drumrack: true, rows: [], notes: [], genres: [], rev: -1 };
let picker = null;
let pickerKnob = listKnobInit();
let touched = -1;
let shift = false;
let genreTravel = 0;
let dirty = true;
let framesSinceDraw = 0;

function get(key) { const v = host_module_get_param(key); return v == null ? '' : v; }
function number(key, fallback) { const n = parseInt(get(key), 10); return Number.isFinite(n) ? n : fallback; }
function set(key, value) { host_module_set_param(key, String(value)); dirty = true; }
function clamp(n, lo, hi) { return Math.max(lo, Math.min(hi, n)); }
function decode(v) { return v <= 63 ? v : v >= 65 ? v - 128 : 0; }
function mini(x, y, value, color = 1) { fontPrint4x5({ fillRect: fill_rect }, x, y, String(value).toUpperCase(), color); }
function fit(value, width) {
  let s = String(value).toUpperCase();
  while (s && fontWidth4x5(s) > width) s = s.slice(0, -1);
  return s;
}
function parseGenres(raw) {
  const result = [];
  let start = 0;
  for (const part of String(raw).split('|')) {
    const i = part.lastIndexOf(':');
    if (i < 0) continue;
    const count = parseInt(part.slice(i + 1), 10) || 0;
    if (count > 0) { result.push({ name: part.slice(0, i), start, count }); start += count; }
  }
  return result;
}
function genreIndex() {
  for (let i = 0; i < g.genres.length; i++) {
    const x = g.genres[i];
    if (g.pattern >= x.start && g.pattern < x.start + x.count) return i;
  }
  return 0;
}
function refresh(force = false) {
  const rev = number('preview_rev', 0);
  if (force || rev !== g.rev) {
    g.rev = rev;
    g.count = Math.max(1, number('pattern_count', 1));
    g.pattern = number('pattern', 0);
    g.steps = clamp(number('steps', 16), 1, 32);
    g.name = get('pattern_name');
    g.genre = get('pattern_genre');
    for (let i = 0; i < VOICES.length; i++) g.rows[i] = get('row' + i);
  }
  if (!g.genres.length) g.genres = parseGenres(get('genre_list'));
  g.swing = number('swing', g.swing);
  g.drumrack = get('note_map') !== 'gm';
  for (let i = 0; i < VOICES.length; i++) g.notes[i] = number(VOICES[i][1], g.notes[i] || 36);
  dirty = true;
}
function usedVoices() {
  const a = [];
  for (let i = 0; i < VOICES.length; i++) if (g.rows[i]) a.push(i);
  return a;
}
function padLabel(note) { return g.drumrack && note >= 36 && note <= 51 ? 'P' + (note - 35) : String(note); }
function pattern(index) {
  if (index === g.pattern) return;
  set('pattern', index);
  refresh(true);
}
function movePattern(delta) {
  const ge = g.genres[genreIndex()] || { start: 0, count: g.count };
  const offset = (((g.pattern - ge.start + delta) % ge.count) + ge.count) % ge.count;
  pattern(ge.start + offset);
}
function moveGenre(delta) {
  if (!g.genres.length) return;
  const i = ((genreIndex() + delta) % g.genres.length + g.genres.length) % g.genres.length;
  pattern(g.genres[i].start);
}
function editNote(knob, delta) {
  const voice = usedVoices()[knob];
  if (voice === undefined) return;
  const lo = g.drumrack ? 36 : 0, hi = g.drumrack ? 51 : 127;
  const next = clamp(g.notes[voice] + delta, lo, hi);
  if (next !== g.notes[voice]) { g.notes[voice] = next; set(VOICES[voice][1], next); }
}
function openList(knob) {
  if (knob === 6) return; // Swing is numeric.
  pickerKnob = listKnobInit();
  if (knob === 7) {
    if (!g.genres.length) return;
    picker = { kind: 'genre', title: 'GENRE', options: g.genres.map(x => x.name),
      index: genreIndex(), original: genreIndex() };
  } else if (knob >= 0 && knob < 6) {
    const voice = usedVoices()[knob];
    if (voice === undefined) return;
    const lo = g.drumrack ? 36 : 0, hi = g.drumrack ? 51 : 127;
    const options = [];
    for (let n = lo; n <= hi; n++) options.push(padLabel(n));
    picker = { kind: 'note', voice, lo, title: VOICES[voice][0] + ' PAD', options,
      index: g.notes[voice] - lo, original: g.notes[voice] - lo };
  } else if (knob === -1) {
    const ge = g.genres[genreIndex()] || { name: g.genre, start: 0, count: g.count };
    const options = [];
    for (let i = 0; i < ge.count; i++) options.push(String(get('name@' + (ge.start + i)) || 'Pattern ' + (i + 1)));
    picker = { kind: 'pattern', start: ge.start, title: ge.name, options,
      index: g.pattern - ge.start, original: g.pattern - ge.start };
  } else if (knob === -2) {
    picker = { kind: 'map', title: 'NOTE MAP', options: ['DRUMRACK', 'GM'],
      index: g.drumrack ? 0 : 1, original: g.drumrack ? 0 : 1 };
  }
  if (picker) picker.touchKnob = knob >= 0 ? knob : -1;
  dirty = true;
}
function choose() {
  if (!picker) return;
  const p = picker;
  picker = null;
  if (p.kind === 'genre') pattern(g.genres[p.index].start);
  if (p.kind === 'pattern') pattern(p.start + p.index);
  if (p.kind === 'note') { g.notes[p.voice] = p.lo + p.index; set(VOICES[p.voice][1], g.notes[p.voice]); }
  if (p.kind === 'map') { set('note_map', p.index ? 'gm' : 'drumrack'); refresh(true); }
  dirty = true;
}
function drawKnob(index, y, used) {
  const x = (index % 4) * 32;
  const voice = index < 6 ? used[index] : undefined;
  const label = index < 6 ? (voice === undefined ? 'K' + (index + 1) : VOICES[voice][0])
    : index === 6 ? 'SWNG' : 'GENR';
  const value = index < 6 ? (voice === undefined ? '--' : padLabel(g.notes[voice]))
    : index === 6 ? g.swing + '%' : (g.genres[genreIndex()] || { name: g.genre }).name;
  if (touched === index) fill_rect(x, y, 31, 15, 1);
  const color = touched === index ? 0 : 1;
  mini(x + 2, y + 1, fit(label, 28), color);
  mini(x + 2, y + 8, fit(value, 28), color);
}
function drawGrid() {
  const used = usedVoices();
  for (let i = 0; i < 4; i++) drawKnob(i, 0, used);
  fill_rect(0, 15, 128, 1, 1);
  const ge = g.genres[genreIndex()] || { start: 0, count: g.count };
  mini(1, 17, fit(g.name || 'PATTERN', 100));
  const pos = (g.pattern - ge.start + 1) + '/' + ge.count;
  mini(127 - fontWidth4x5(pos), 17, pos);
  const visible = Math.min(6, used.length);
  const stepW = 126 / g.steps;
  for (let r = 0; r < visible; r++) {
    const voice = used[r], row = g.rows[voice], y = 24 + r * 4;
    for (let s = 0; s < g.steps && s < row.length; s++) {
      if (row[s] === '.') continue;
      const x = 1 + Math.floor(s * stepW);
      const w = Math.max(1, Math.floor((s + 1) * stepW) - Math.floor(s * stepW) - 1);
      fill_rect(x, y + (row[s] === 'g' ? 2 : 1), w, row[s] === 'g' ? 1 : 2, 1);
    }
  }
  fill_rect(0, 48, 128, 1, 1);
  for (let i = 4; i < 8; i++) drawKnob(i, 49, used);
}
function drawList() {
  const p = picker;
  const count = p.options.length;
  const position = (p.index + 1) + '/' + count;
  mini(1, 1, fit(p.title, 102 - fontWidth4x5(position)));
  mini(127 - fontWidth4x5(position), 1, position);
  fill_rect(0, 8, 128, 1, 1);
  const visible = 8;
  const start = clamp(p.index - 3, 0, Math.max(0, count - visible));
  for (let row = 0; row < visible && start + row < count; row++) {
    const index = start + row, y = 10 + row * 6;
    const selected = index === p.index;
    if (selected) fill_rect(0, y - 1, 128, 6, 1);
    const color = selected ? 0 : 1;
    mini(2, y, selected ? '>' : ' ', color);
    mini(9, y, fit(p.options[index], 108), color);
    if (index === p.original) mini(121, y, '*', color);
  }
  fill_rect(0, 57, 128, 1, 1);
  mini(1, 59, 'TURN SCROLL');
  mini(73, 59, p.touchKnob >= 0 ? 'LIFT PICK' : 'CLICK PICK');
}
function draw() {
  clear_screen();
  if (picker) drawList();
  else drawGrid();
  dirty = false;
  framesSinceDraw = 0;
}
function midi(data) {
  if (!data || data.length < 3) return;
  const type = data[0] & 0xF0, a = data[1], b = data[2];
  if ((type === 0x90 || type === 0x80) && a < 8) {
    if (type === 0x90 && b > 0) { touched = a; if (!picker && a !== 6) openList(a); }
    else if (touched === a) {
      if (picker && picker.touchKnob === a) choose();
      touched = -1;
    }
    dirty = true;
    return;
  }
  if (type !== 0xB0) return;
  if (a === 49) { shift = b > 0; return; }
  if (a === 3 && b > 0) { if (picker) choose(); else openList(shift ? -2 : touched >= 0 ? touched : -1); return; }
  const delta = decode(b);
  if (!delta) return;
  if (picker) {
    const step = a === 14 ? delta : a >= 71 && a <= 78
      ? listKnobStep(pickerKnob, delta, Date.now(), picker.options.length) : 0;
    if (step) {
      picker.index = clamp(picker.index + step, 0, picker.options.length - 1);
      dirty = true;
    }
    return;
  }
  if (a === 14) { (shift ? moveGenre : movePattern)(Math.sign(delta)); return; }
  if (a >= 71 && a <= 76) { editNote(a - 71, Math.sign(delta)); return; }
  if (a === 77) { const next = clamp(g.swing + Math.sign(delta) * 5, 0, 100); if (next !== g.swing) { g.swing = next; set('swing', next); } return; }
  if (a === 78) {
    if (Math.sign(delta) !== Math.sign(genreTravel)) genreTravel = 0;
    genreTravel += Math.sign(delta);
    if (Math.abs(genreTravel) >= 24) { moveGenre(Math.sign(genreTravel)); genreTravel = 0; }
  }
}
globalThis.chain_ui = {
  init() { g.genres = []; picker = null; touched = -1; shift = false; genreTravel = 0; refresh(true); },
  tick() { if (dirty || ++framesSinceDraw >= 12) draw(); },
  onMidiMessageInternal: midi,
  handleBack() { if (!picker) return false; picker = null; dirty = true; return true; },
};
