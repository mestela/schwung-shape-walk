# Shape Walk for Schwung

A MIDI effect that generates a repeatable sequence of chords by moving two fourth/fifth dyads around Move's chromatic-fourths grid. Each hand can hold a fourth, hold a fifth, or choose either at each step. The left and right dyads can travel together or separate. A colour note can sit one chromatic pad beside a dyad note, and an optional bass voice can sit beneath the upper shape.

## Install

**From Schwung Manager:** Under **Install Custom Module → From GitHub URL**, enter `https://github.com/mestela/schwung-shape-walk`. This installs **Shape Walk Pads**, which generates and plays chords on its own. To add the **Shape Walk MIDI FX**, download `shape-walk-module.tar.gz` from the [latest release](https://github.com/mestela/schwung-shape-walk/releases/latest) and use **Install Custom Module → From Tarball**. The manager installs one module per action; do not give it the combined archive.

**Manual installation:** Download `shape-walk-v0.7.0.tar.gz` from the [latest release](https://github.com/mestela/schwung-shape-walk/releases/latest). Extract it into `/data/UserData/schwung/modules/` on your Move, then restart Schwung. The archive installs **Shape Walk** under `midi_fx/` and **Shape Walk Pads** under `tools/`. It does not contain or replace your saved chords in `/data/UserData/schwung/shape-walk-seq.json`.

Requires [Schwung](https://github.com/mestela/schwung) on Ableton Move.

## Shape Walk Pads (Overtake companion)

Open **Tools → Shape Walk Pads**. The companion imports the first available live Shape Walk MIDI FX recipe on first launch. The left 4×4 grid holds 4, 8, or 16 generated chords according to that recipe; the right 4×4 grid holds saved chords.

- Play any left pad to hear a generated chord. Tap an empty right pad to copy the last played left chord there. The right pad plays it immediately and remains playable at any time, with no step timing. Playing a saved pad opens its chord editor: knobs 1–4 edit the main voices, knob 5 edits the bass when present, knob 6 edits an extra colour voice when present, and knob 8 transposes the whole chord from −12 to +12 semitones. Shift + knob moves by an octave. Play a blue left pad, press the jog wheel, or use step buttons 1–3 to return to the generator controls.
- Press **Copy** on a selected saved pad, then tap a destination right pad to paste. You can hold Copy while tapping the destination or release it first. The copied chord can be edited and transposed independently. Copy can replace an occupied right pad.
- Hold **X** and tap a right pad to clear it. Saved pads are snapshots: changing the seed changes the left grid, not the right grid. Existing sixteenth-note patterns are migrated into the saved pad bank.
- Up/Down changes the seed; Shift + Up imports the current Shape Walk MIDI FX recipe again. Track buttons 1–4 choose the Move track MIDI channel.
- The primary screen uses Schwung's shared knob-grid renderer, with its parameter widgets, value-on-touch display, bank bar and footer. Turn the jog wheel to browse its three knob banks, or use step buttons 1–3 as shortcuts; knobs 1–8 edit the displayed values, and Shift + knob makes coarse numeric changes. Enum knobs such as Tonality show Schwung's option list while turning and take six detents per choice; Shift turns them one choice per detent. Press the jog wheel (or step button 4) to toggle the chord and track status view. Changing a knob while it is open returns to the parameter screen. These controls update the generated left bank while keeping saved right pads intact. The MIDI FX's clock and trigger controls do not apply to free pad playing.
- Play and Record still reach Move. Pad notes are sent to Move's track input, so you can perform the saved pads into Move's recorder. Back suspends the editor and releases held notes; Shift + Back exits. The saved bank and selected track live in `/data/UserData/schwung/shape-walk-seq.json`.

## Play it

Load **Shape Walk** into a Schwung Signal Chain MIDI FX slot before a sound generator.

- **Pads mode:** notes 48–55 (C3–G3, with default eight steps) play the generated sequence one chord at a time. Set *First Trigger* to match another pad range. The incoming note selects the step; it does not transpose the chord. Releasing the pad ends it.
- **Clock mode:** Press Move's Play button: MIDI Start plays step one, then the effect advances one chord every 1, 1.5, 2, 3, 4, or 6 beats according to *Clock Rate*. The 1.5, 3, and 6 beat options are dotted values. *Sustain %* controls how long each chord sounds within that interval: 100 holds until the next chord, 50 releases halfway through. MIDI Stop releases the sounding chord. Other incoming notes pass through. Switching to Clock while the transport is already playing starts the sequence immediately. Turning knobs while Clock is running keeps the current chord and beat count; regenerated notes and rate or sustain changes take effect at the next chord boundary.

Use *Seed* to reproduce a sequence. The same seed and settings produce the same chord pitches, including the per-step fourth/fifth choices when a hand is set to *either*. Save a Signal Chain patch to retain the full recipe. Fixed fourth/fifth settings retain their 0.1.0 sequences; *either* was added in 0.1.1. The bass was added in 0.1.2. Later algorithm changes may change seeds.

The default path follows grid motions down one row (−5), left two and up one (+3), down one (−5), then up one and right two (+7) to return to the starting position. *Wander* sometimes replaces a move; *Split Hands* sometimes shifts only the right dyad. *Continuity* prefers smaller random moves. *Colour Chance* adds an adjacent semitone; *Tension* favours tritone relationships over third relationships with the note's dyad partner. These are tendencies, not chord quality filters.

*Root Note* selects one of the 12 chromatic notes from C through B, with the left-hand starting note placed in the octave beginning at middle C. Existing patches with a Root MIDI value keep the same note name.

*Min Hand Gap* sets the minimum spacing between the highest note of the left dyad and the lowest note of the right dyad. At `7`, the two hand shapes stay at least a perfect fifth apart, even when *Split Hands* moves the right hand toward the left. Its default of `0` preserves earlier seeds. This constraint applies to the four core notes; an added colour note can still sit a semitone beside one of them.

*Smoothness* (0–100) repairs an increasing proportion of steps by moving the right dyad and, when enabled, bass a few semitones to reduce semitone, whole-tone, and tritone clashes across the complete chord. It leaves each hand's fourth/fifth shape intact. It also reduces the frequency of adjacent colour notes, reaching none at 100. At 0, the original seeded sequence is unchanged. The score is a roughness heuristic, so this is a sound-shaping control rather than a fixed major/minor scale filter.

*Tonal Colour* nudges voices relative to each chord's moving left-hand root. **Major** and **minor** add their third; **2nd**, **6th**, **b6**, and **b7** add the named interval. **Maj7** uses root, major third, major seventh, and sixth; **min7** uses root, minor third, minor seventh, and fourth. **Maj9** and **min9** use the matching third, seventh, and ninth. The seventh and ninth settings repurpose three of the four upper voices, so they have a stronger character and intentionally allow the characteristic close intervals. **Open** leaves the geometry alone. *Tonal Pull* (0–100) sets how often that change is attempted. At Pull 0 the old seeded sequence is unchanged.

*Inversion* is on the Pads tool's Bass bank, knob 4, with **root**, **1st**, **2nd**, and **3rd** choices. It raises the lowest one, two, or three upper voices above the rest by octaves, retaining the chord's pitch classes. When bass is enabled, it follows the new lowest upper voice at a nearby lower octave. Saved right-hand pads are snapshots and keep their pitches when the generator inversion changes.

### Bass experiment

*Bass Mode* defaults to **off**, so existing seeds sound as before. **Follow** adds a bass note that transposes with the upper shape. **Wander** gives the bass its own seeded walk by one semitone or one fourth in either direction; *Bass Motion* sets how often it moves. The bass is kept below the upper chord, using octave placement when needed.

*Bass Offset* is the interval below the left-hand anchor, from `0` to `-24` semitones. It defaults to `-12`, the same note name one octave down. With Root Note C, `-12` starts on C and `-14` starts on B♭ below it. The bass remains below the upper chord when its independent walk moves close to the dyads. Compare the same seed with Bass Mode off, follow, and wander: the upper notes remain identical.

With Bass Mode enabled, Shape Walk sends the bass note last. A four-voice instrument such as Braids otherwise steals that first low note when the upper notes arrive. When a chord has more notes than the instrument has voices, the instrument will still omit some upper notes.

## Build

`make test` runs local logic checks. `make device` requires an `aarch64-linux-gnu-gcc` toolchain and the Schwung source headers at `../schwung/src` (override `SCHWUNG_ROOT`). `make package` creates the combined release archive and separate module archives in `dist/`.

Shape Walk and Shape Walk Pads have been tested on Ableton Move. Seeded results are repeatable within this release; future algorithm updates may change a seed's chords.
