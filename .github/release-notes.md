NightShift is a free music studio for Windows and Mac: patterns and a step sequencer, a piano roll, a mixer, built-in synths and drum kits, and an AI band (drummer, bass, guitar, keys and synth) that writes parts in the style you describe.

## What's new in 1.0.1

- **Double-clicks in Arrange work as in Logic.** Double-click a track's header and its instrument opens, every time: Nova's panel for a synth, a plugin's own window, the drummer for a drum kit, or Steps for a sampled sound. Double-click a clip and its notes open in the piano roll, drums included.

## New in 1.0

Everything that was on NightShift's "Not yet" list is in, plus free sample packs and TONE3000.

### Write and edit

- **Record automation by moving a control.** Switch on the record-automation button beside Record, then move a fader, a knob, a plugin's control or your MIDI controller's knob while the song plays: its line is written as you go.
- **Curved automation, and automation on buses.** Drag the dot in the middle of a line to bend it. A bus's volume, pan and effects get lanes of their own.
- **Curved tempo glides.** A glide can ease in or ease out: drag the ring in its middle.
- **The AI band across time signatures.** Ask for "a rock song with a bridge in 7/8" and each section is written in its own time signature.

### Audio

- **Time-stretching.** An audio clip can follow the song's tempo at its own pitch, play faster or slower, or move up or down by semitones. Ctrl+drag a clip's end (Cmd on a Mac) to stretch it to fit. Loops you drop in follow the tempo when theirs is known.
- **Pitch Correction**, a new effect: it pulls singing or playing onto the song's key, from natural to robotic, and it stays on while you sing through it.
- **The NAM amp** matches a model's input to your audio interface's level, as NAM's own plugin does, and models that come in sizes get a Size control that uses less of your computer.

### Sounds

- **Samples**, in the sidebar: your Downloads, Music and any folders you add. Click a sample to hear it; drag it into Steps or Arrange.
- **Free sample packs**, offered the first time you open Samples. NightShift Sounds 1 has 345 sounds made by NightShift: 808, 909 and kit drum hits, every Nova synth sound, and four-bar loops in 16 styles named with their tempo and key, free to use however you like (CC0). The studio drum kits, grand piano and orchestra are in the same list.
- **TONE3000 in the NAM amp.** Click **Find a tone on TONE3000**, sign in with your own free TONE3000 account and pick a tone: NightShift downloads it into the amp, with its creator and licence. Powered by [TONE3000](https://www.tone3000.com).

### Hardware, files and export

- **MIDI out to hardware synths.** Choose "Synth plugged in" as a channel's instrument: NightShift plays it in time, and can send tempo to drum machines and arpeggiators.
- **MIDI files.** Open or drop a .mid to bring in its tracks, tempo and time signatures, and export the song or a pattern as one.
- **A new Export window** (Ctrl+R): WAV, MP3 up to 320 kbps, stems (one file for each channel and bus), or a MIDI file. Every export's first beat now lands exactly on time.
- **Freeze** a channel to play it from audio and give your computer a rest; unfreeze it any time.

Earlier: 0.4 gave the AI band ears for a song's vibe and added Check for updates (in the ⋯ menu, which offers you this version too); 0.3.0 gave the drummer a real drummer's feel; 0.2.0 brought 13 new styles. NightShift is free and always will be; donating is entirely up to you.

## Install

**Windows 10 or 11 (64-bit):** download `NightShift-Setup-….exe` below and run it. It installs just for you, with no admin password. The setup isn't signed (signing costs money), so Windows may say "Windows protected your PC": click **More info**, then **Run anyway**.

**Mac with Apple Silicon (M1 or newer, macOS 11 or later):** download `NightShift-…-mac.pkg` below and double-click it. It isn't signed by Apple (that needs a paid developer account), so the first time macOS won't open it: click **Done**, then open **System Settings > Privacy & Security**, scroll down to the line about NightShift, click **Open Anyway** and run the installer. It puts NightShift in Applications.

Both include the NightShift plugins, which other music apps can use too: **Lofi** (Cassette, Vinyl, Crush, Haze), **Bass** (Drive, Split) and **Nightshift Drums**.

## Sounds

The 808 and 909 kits and the synth sounds are built in, so you can start making music straight away. The free sample pack, the studio drum kits, the grand piano and the orchestra download the first time you ask for one, from GitHub or from the people who recorded them (DrumGizmo, Salamander Grand Piano, Versilian Studios).

## Source

NightShift is free software under the GNU GPL v3 or later. Its source is in this repository; the plugins' source is in [nightshift-lofi](https://github.com/ExfilBravo/nightshift-lofi), [nightshift-bass](https://github.com/ExfilBravo/nightshift-bass) and [nightshift-drums](https://github.com/ExfilBravo/nightshift-drums).
