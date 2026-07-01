# Beat Bank

A library of **standard drum patterns** for [Schwung](https://github.com/charlesvestal/schwung) on Ableton Move.

No generation, no randomness, no fills. Beat Bank is a chainable `midi_fx`
that **prints** one of ~100 hand-authored, canonical drum grids straight into
Move's step sequencer — so you can drop in a recognizable groove from almost
any genre instead of programming one by hand.

Pick a pattern from a list; it splats one bar into Move's clip (which Move
records). That's the whole tool — no tempo, swing, preview, or note editing.
It plays the grid straight at Move's tempo, so it never competes with Move's
own groove.

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

~100 patterns across: hip-hop / boom-bap, lofi, trap, trip-hop · house, techno,
electro, disco, UK garage · drum & bass, jungle, breakbeat, dubstep · funk
(Funky Drummer, Cold Sweat, Purdie shuffle…), soul/Motown, rock/pop, gospel,
country · bossa nova, samba, Latin, Afrobeat, reggae/dub, calypso.

## Controls

Dead simple. Beat Bank uses Schwung's standard chain menu (so it's fully
swappable). Open the slot's **MIDI FX** and you get two entries:

- **Pattern** — a scrolling list of the whole library (name + genre). **Land on
  a pattern and it prints one bar into Move** (auto, after a brief settle).
  Scroll to another to print a different one.
- **Swap Module** — change Beat Bank for another MIDI FX, or pick **None** to
  clear it.

No preview, no note editing, no buttons — pick a pattern, it splats.

## Voices

Twelve voices, each sending a **General MIDI drum note** (Kick 36, Snare 38,
Closed Hat 42, Open Hat 46, Clap 39, Rim/Clave 37, Tom 45, Ride 51, Crash 49,
Cowbell 56, Conga 63, Perc 70). Use a **GM-mapped drum kit** on the Move track
so they line up; if your kit maps pads differently, remap on the Move side.

## Print into Move's native sequencer

Beat Bank prints the selected pattern as one clean bar that Move records into a
clip you own and can edit/switch natively.

1. On **Move**: pick the drum track to fill, note its MIDI channel (track *N*
   defaults to channel *N*). Its drum kit owns the 16 pads.
2. In **Schwung**, on a shadow slot: set **MIDI FX = Beat Bank**, switch the
   slot's **MIDI FX mode to Schw+Move**, set the slot's **Receive Channel** to
   that track's channel, and **mute the slot** (a slot needs a sound generator
   to be valid — load any; injection is on the MIDI path and keeps working).
3. On **Move**: **arm Record** on that track and start playing.
4. Open **Pattern**, scroll to the one you want — it prints one bar and Move
   records it. Scroll to another to replace it.

> Beat Bank can inject the notes but can't operate Move's transport, so **Move
> must be playing + recording** to capture. Injected notes are the GM drum map;
> Cowbell/Conga/Perc sit above the usual 36–51 pad range, so use a kit that maps
> them or they won't sound.
>
> The record-capture path was confirmed on hardware (Move records external
> USB/injected MIDI into its step sequencer). The exact slot wiring (Schw+Move
> + Receive Channel) should be sanity-checked on your device the first time.

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
