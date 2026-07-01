# Beat Bank

A library of **standard drum patterns** for [Schwung](https://github.com/charlesvestal/schwung) on Ableton Move.

No generation, no randomness, no fills. Beat Bank is a chainable `midi_fx`
drum sequencer for Schwung's Signal Chain. It loops one of ~450 hand-authored,
canonical drum grids and **plays the sound generator in the same slot** — e.g.
an SF2 drum kit — using General MIDI drum notes. Same idea as
[Super Arp](https://github.com/handcraftedcc/schwung-superarp): the module
makes the notes, the chain feeds them to the synth.

Stays entirely in the Schwung world — no Move-track mixing. Pick a pattern from
a list; it loops at Move's tempo (silent when the transport is stopped).
Record it the Schwung way with the Quantized Sampler (**Shift+Sample**).

## Patterns are editable files — add your own

Patterns are **not** baked into the binary. They load at startup from plain
`.beat` text files, scanned from two folders and merged:

1. **Shipped defaults** — `…/modules/midi_fx/beatbank/patterns/*.beat`
   (refreshed on every module upgrade)
2. **Your patterns** — `/data/UserData/schwung/beatbank/patterns/*.beat`
   (persist across upgrades; the module seeds a `_HOWTO.txt` here on first run)

A `.beat` file is a list of pattern blocks separated by blank lines:

```
name: Boom Bap
genre: LOFI
steps: 16
kick:  x.......x.......
snare: ....A.......A...
ch:    x.x.x.x.x.x.x.x.
```

- `steps` is `16` (one bar of 16th notes) or `32` (two bars)
- Row characters: `.` rest · `x` hit · `A` accent · `g` ghost (soft)
- Omit voices that don't play; rows must be exactly `steps` long
- Drop a file in the user folder (or copy a shipped one and edit it), reload
  the module, and your patterns appear after the built-ins

Edit them over SSH, the Schwung Manager file browser (`move.local:7700/files`),
or anywhere you can drop a text file.

## Genres included

~450 patterns across 24 genres: hip-hop / boom-bap, lofi, trap, trip-hop ·
house, techno, electro, disco, UK garage · drum & bass, jungle, breakbeat,
dubstep · funk (Funky Drummer, Cold Sweat, Purdie shuffle…), soul/Motown,
rock/pop, gospel, country · bossa nova, samba, Latin, Afrobeat, reggae/dub,
calypso. Browse them genre-first: jog cycles within a genre, Knob 1 switches
genre.

## Setup & use

1. In a Schwung Signal Chain slot, load a **drum sound generator** — e.g. the
   SF2 module with a GM/percussion kit — as the slot's Sound Generator.
2. Load **Beat Bank** as that slot's **MIDI FX**.
3. Open Beat Bank's menu → **Pattern** → scroll to a pattern. It loops whenever
   Move's transport is playing; you hear it through the drum kit.
4. **Record** with the Quantized Sampler (**Shift+Sample**) — it resamples the
   chain's audio to a WAV in `Samples/Schwung/`.

Menu: **Pattern View** · **Globals** · **Swap / None**.

- **Pattern** — a fullscreen grid view with **genre-first** browsing and live
  pad editing:
  - **jog** cycles patterns *within the current genre*; **Knob 1** switches
    genre; **Knob 2** sets swing (header shows `GENRE · name · pos/count`).
  - **Knobs 3–8 re-point each drum voice to a different pad** (drum-rack) or
    note (GM), live — no trip into the sampler. Knobs are **positional**: **K3**
    edits the 1st row, **K4** the 2nd, … up to **K8** (6th). Rows are in fixed
    order (kick, snare, hats, then extras), so a voice keeps its pad across
    patterns — only *which* knob reaches it changes with what the pattern plays.
  - Each voice row shows the note / **drum-rack pad number** (`p1`…`p16`) it
    drives *and its knob* (`K3`…) — so you can match a loaded MrDrums kit
    without leaving Beat Bank. Pad edits **persist with the slot** (switching
    Note Map resets them).
  - **jog-click / Back** exits.
- **Globals** — **Swing** (delays the off-beat 16ths; deterministic, works on
  any synth — Move's Groove doesn't apply since we bypass its sequencer) and
  **Note Map** (below).

It follows Move's tempo and plays straight; swing is the only groove control.

## Voices & Note Map

Twelve voices. The **Note Map** setting chooses which MIDI notes they send:

- **`gm`** — full General MIDI drum map (Kick 36, Snare 38, Closed Hat 42,
  Open Hat 46, Clap 39, Rim 37, Tom 45, Ride 51, Crash 49, Cowbell 56,
  Conga 63, Perc 70). Use with **SF2 / soundfont** GM kits.
- **`drumrack`** (default) — everything squeezed into **36–51**, the
  Move/Ableton drum-rack range that every Schwung drum sampler (MrDrums, Forge,
  KrautDrums, Libpo32, …) triggers on. Cowbell/Conga/Perc move to free pads
  (47/48/50). Use with **pad samplers**.

Beat Bank **defaults to `drumrack`** because nearly all the catalog's drum
samplers trigger on 36–51, *not* the sparse GM range — in `gm` mode
Cowbell/Conga/Perc (56/63/70) are silent on them. Only switch to `gm` for
**SF2 / soundfont** GM kits.

## Build & install

```bash
./scripts/build.sh            # cross-compile dsp.so (Docker or native)
./scripts/install.sh          # deploy to move.local (incl. the patterns/ folder)
./scripts/package.sh          # or build a release tarball
```

Tests (no hardware required):

```bash
make test                     # validate the .beat library + parser + sequencer timing
make validate                 # just lint the .beat files
```

## Releasing

Tag-driven GitHub Actions (`.github/workflows/release.yml`) builds in Docker,
attaches `beatbank-module.tar.gz`, and updates `release.json`. To cut a release:

```bash
git tag v0.1.0 && git push --tags
```

## Notes

- The only real interchange "standard" for drum patterns is the MIDI file, but
  those bake in tempo/timing and are awkward to hand-edit — the whole point
  here is a human-editable grid, so Beat Bank uses the `.beat` text format.
  (MIDI-file import could be added later as a converter.)
- The Amen/Think/clave patterns are quantized templates; the original
  recordings have microtiming a 16th grid can't capture — they'll sound a touch
  stiffer than a sampled break, which is correct for a step voicing.

## Pattern sources

Canonical placements were cross-checked against MusicRadar, Attack Magazine,
Native Instruments, Drumeo, Modern Drummer, Studio Brootle, BVKER, MIDI Mighty,
Wikipedia and others (per-genre references are in the research notes).

## Acknowledgements

Built on the Schwung MIDI FX plugin API; the clock-handling skeleton follows
the same shape as the `branchage` module.
