# 🥁 Beat Bank

**A library of 450+ classic drum patterns for [Schwung](https://github.com/charlesvestal/schwung) on the Ableton Move.**

![patterns](https://img.shields.io/badge/patterns-457-6C5CE7)
![genres](https://img.shields.io/badge/genres-24-00B894)
![platform](https://img.shields.io/badge/platform-Ableton%20Move-2D3436)
![module](https://img.shields.io/badge/Schwung-MIDI%20FX-0984E3)

No generation, no randomness, no fills — just **hand-authored, canonical grooves**
you already recognize. Beat Bank is a chainable `midi_fx` module for Schwung's
Signal Chain: it loops a drum grid and **plays whatever drum synth is in the same
slot**. The module makes the notes; the chain feeds them to the sound generator.

> Boom-bap, house, techno, jungle, funk, bossa, afrobeat… pick a beat, press play,
> and dial in the sounds — all without leaving the Schwung world.

---

- [Quick start (with Mr. Drums)](#-quick-start-with-mr-drums)
- [The Pattern view](#-the-pattern-view)
- [Controls](#-controls)
- [Note Map](#-note-map)
- [Genres](#-genres)
- [Add your own patterns](#-add-your-own-patterns)
- [Build & install](#-build--install)

---

## 🚀 Quick start (with Mr. Drums)

Beat Bank is a *driver* — it needs a drum synth to make sound. **Mr. Drums** is a
perfect partner: it loads Move/Ableton `.ablpreset` drum kits (or your own
samples) and triggers on the standard 36–51 pad range, which is exactly what Beat
Bank sends by default.

1. **Install both modules** from the [Schwung Manager](http://move.local:7700)
   (or `./scripts/install.sh`): **Beat Bank** and **Mr. Drums**.
2. **Open the Schwung shadow UI** on your Move and pick a chain **slot**.
3. **Sound Generator → Mr. Drums.** Load a drum kit (a built-in kit, an
   `.ablpreset`, or your own samples).
4. **MIDI FX → Beat Bank.**
5. Open Beat Bank → **Pattern**, then **press Play** on the Move. You'll hear the
   beat playing through your kit. 🎉
6. **Turn the jog** to audition patterns in the current genre; **Knob 8** hops to
   another genre.
7. **Turn Knobs 1–6** to move each drum voice onto a different pad in your kit —
   live, while the loop plays. No trips into the sampler.
8. **Knob 7** adds swing. When it sounds right, **record it** with the Quantized
   Sampler (**Shift + Sample**) → a WAV lands in `Samples/Schwung/`.

That's the whole loop: **pick a beat → match it to your kit → capture it.**

---

## 🎛 The Pattern view

A fullscreen grid. Genre-first browsing on the jog, live sound design on the knobs.

```
┌──────────────────────────────────────┐
│ HOUSE     Classic House         1/19  │
│ 1 KCK p1  x...x...x...x...            │
│ 2 CH  p7  x.x.x.x.x.x.x.x.            │
│ 3 OH  p11 ..x...x...x...x.            │
│ 4 CLP p4  ....x.......x...            │
│                                       │
│ K7 sw:0            K8 genre           │
└──────────────────────────────────────┘
```

<sub>Row = knob · voice · pad · the 16-step grid (`x` hit · `.` rest). Turn Knob 3
here and the closed-hat jumps to a different pad in your kit, live.</sub>

Each row shows its **knob number**, the **voice**, and the **pad** (`p1`…`p16`) it
drives — so you can see at a glance which knob re-points which drum, and match a
loaded kit without guessing.

---

## 🎚 Controls

| Control            | Action                                                            |
| ------------------ | ----------------------------------------------------------------- |
| **Jog** turn       | Browse patterns *within* the current genre                        |
| **Knobs 1–6**      | Move each voice (row 1–6) to a different **pad / note** — live     |
| **Knob 7**         | Swing (0–100)                                                     |
| **Knob 8**         | Switch genre                                                      |
| **Jog-click / Back** | Exit the Pattern view                                           |

**Positional pads.** Knob 1 always edits the top row, Knob 2 the next, and so on.
Voices appear in a fixed order (kick, snare, hats, then extras), so a voice keeps
its pad across patterns — only *which* knob reaches it shifts with what the
current pattern plays. Pad edits **persist with the slot** (switching Note Map
resets them to the map defaults).

**Tempo & feel.** Beat Bank follows Move's transport (silent when stopped) and
plays straight. Swing is the only groove control — it's deterministic and works
on any synth, since Beat Bank bypasses Move's sequencer (so Move's Groove doesn't
apply). Genre and pattern selection live under **Menu → Pattern**; swing and note
map are also under **Menu → Globals**.

---

## 🗺 Note Map

Twelve voices — kick, snare, closed/open hat, clap, rim, tom, ride, crash,
cowbell, conga, perc. **Note Map** picks which MIDI notes they send:

| Map                    | Notes                                        | Use with                             |
| ---------------------- | -------------------------------------------- | ------------------------------------ |
| **`drumrack`** *(default)* | Everything in **36–51** (the Move drum-rack range) | **Pad samplers** — Mr. Drums, Forge, KrautDrums, Libpo32… |
| `gm`                   | Full General MIDI drum map (Kick 36 … Perc 70) | **SF2 / soundfont** GM kits          |

Nearly every drum synth in the catalog triggers on **36–51**, so Beat Bank
defaults to `drumrack` (Cowbell/Conga/Perc land on free pads 47/48/50). Only
switch to `gm` for SF2-style kits that expect the sparse General MIDI notes.

---

## 🎵 Genres

**457 patterns across 24 genres:**

> **Urban** — hip-hop / boom-bap · lofi · trap · trip-hop
> **Electronic** — house · techno · electro · disco · UK garage
> **Breaks** — drum & bass · jungle · breakbeat · dubstep
> **Band** — funk *(Funky Drummer, Cold Sweat, Purdie shuffle…)* · soul / Motown · rock / pop · gospel · country
> **World** — bossa nova · samba · Latin · Afrobeat · reggae / dub · calypso

Browse **genre-first**: the jog cycles patterns inside a genre; Knob 8 jumps
between genres. Named grooves (Funky Drummer, Amen, Think, claves, Purdie/Rosanna
shuffles…) are quantized to the 16th grid — recognizable placements rather than
exact swung transcriptions.

---

## ✍️ Add your own patterns

Patterns are **not** baked into the binary — they load at startup from plain
`.beat` text files, merged from two folders:

1. **Shipped defaults** — `…/modules/midi_fx/beatbank/patterns/*.beat` (refreshed on upgrade)
2. **Your patterns** — `/data/UserData/schwung/beatbank/patterns/*.beat` (persist across upgrades; a `_HOWTO.txt` is seeded here on first run)

A `.beat` file is a list of pattern blocks separated by blank lines:

```
name: Boom Bap
genre: LOFI
steps: 16
kick:  x.......x.......
snare: ....A.......A...
ch:    x.x.x.x.x.x.x.x.
```

- `steps` — `16` (one bar of 16ths) or `32` (two bars)
- Row chars — `.` rest · `x` hit · `A` accent · `g` ghost (soft)
- Omit voices that don't play; every row must be exactly `steps` long
- Keep it to **6 voices max** so every voice stays editable on Knobs 1–6

Drop a file in the user folder (or copy a shipped one and edit it), reload the
module, and your patterns appear alongside the built-ins. Edit over SSH, the
[Schwung Manager file browser](http://move.local:7700/files), or any text editor.

---

## 🔧 Build & install

```bash
./scripts/build.sh        # cross-compile dsp.so (Docker or native)
./scripts/install.sh      # deploy to move.local (incl. the patterns/ folder)
./scripts/package.sh      # or build a release tarball
```

Tests — **no hardware required**:

```bash
make test                 # validate the .beat library + parser + sequencer timing
make validate             # just lint the .beat files
```

**Releasing** is tag-driven (`.github/workflows/release.yml` builds in Docker,
attaches `beatbank-module.tar.gz`, updates `release.json`):

```bash
git tag v0.1.0 && git push --tags
```

---

## 📚 Notes & sources

- The only real interchange "standard" for drum patterns is the MIDI file, but
  those bake in tempo and are awkward to hand-edit — the whole point here is a
  **human-editable grid**, so Beat Bank uses the `.beat` text format.
- The Amen / Think / clave patterns are quantized templates; the originals have
  microtiming a 16th grid can't capture, so they'll sound a touch stiffer than a
  sampled break — which is correct for a step voicing.
- Canonical placements were cross-checked against MusicRadar, Attack Magazine,
  Native Instruments, Drumeo, Modern Drummer, Studio Brootle, BVKER, Wikipedia
  and others (per-genre references live in the research notes).

## 🙏 Acknowledgements

Built on the [Schwung](https://github.com/charlesvestal/schwung) MIDI FX plugin
API. Same driver philosophy as [Super Arp](https://github.com/handcraftedcc/schwung-superarp)
— the module makes the notes, the chain plays the synth.
