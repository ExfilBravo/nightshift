# The AI drummer's learned feel

`learn_feel.js` reads Google Magenta's **Groove MIDI Dataset** and writes `src/ai/LearnedFeel.cpp` (from
`LearnedFeel.cpp.in`) and `learned.json` (the same numbers, to read): how real drummers play each style the AI
drummer knows, for it to play that way.

## The dataset

Groove MIDI Dataset v1.0.0 by Jon Gillick, Adam Roberts, Jesse Engel, Douglas Eck and David Bamman (Google
Magenta), licensed CC BY 4.0 (https://creativecommons.org/licenses/by/4.0/):
https://magenta.tensorflow.org/datasets/groove. It's 13.6 hours of ten drummers on a Roland TD-11 electronic kit,
played to a click: 1,150 MIDI files with their style, tempo, beat or fill, and time signature. Cite: Jon Gillick,
Adam Roberts, Jesse Engel, Douglas Eck and David Bamman, "Learning to Groove with Inverse Sequence
Transformations", International Conference on Machine Learning (ICML), 2019.

What's here is derived from it (changed: reduced to averages of timing and velocity per style); the recordings
aren't part of NightShift. The credit is in About NightShift, README.md's Licence section and NOTICES.txt.

## Running it

Download `groove-v1.0.0-midionly.zip` (3.1 MB; sha256 651cbc524ffb891be1a3e46d89dc82a1cecb09a57c748c7b45b844c4841dcc1e)
from https://storage.googleapis.com/magentadata/datasets/groove/groove-v1.0.0-midionly.zip, unzip it anywhere
outside the repository, then from the repository's top folder:

    node tools/groove/learn_feel.js <the unzipped folder>/groove

and build. It prints what it learned for each style (its beats, minutes, swing, spread and each drum's push).

## What it learns

For every recorded beat (not the fills) in 4/4 of the dataset styles a NightShift style or genre learns from (the
`learners` table at the top of the script): each hit on the nearest 16th (or, for a triplet feel, the nearest
8th-note triplet), how far off in ms, each bar's drift against the click taken out. Flams' and drags' grace notes
are left out, as is the kit's crosstalk (a very soft hit on one pad as another is struck), and anything further
than 30% of a step from its step (another subdivision, not a feel). Then, per style:

- **swing**: how much later the offbeat 16ths (or 8ths) land than the beats, as the drummer's swing;
- **push**: each drum's average ms ahead (-) or behind (+), after the swing;
- **timing** and **accent**: each drum at each step of the bar, its average ms against that push and its velocity
  against the drum's own average (only where it's played at least 12 times, and a fifth as often as its busiest
  step; accents kept to 0.5 to 1.6);
- **looseness**: what's left once each beat's shared lean is out too, as a robust spread, as the drummer's
  looseness (its wander is 0.3 times that).

A style the dataset has too little of (under two minutes) keeps its own feel, as do the programmed styles that are
tight on purpose (trap, drill, house, techno, EDM, drum and bass, dubstep, synthwave, reggaeton) and lofi's and
neo-soul's deliberately lazy pocket. Boom bap keeps its MPC swing and late snare, reggae and afrobeats their swing.

When the drummer writes a style with a learned feel, the groove takes its swing, push and looseness and names
the feel (`"learned"` in its JSON); when it's played into patterns, each hit moves by its step's timing and is
accented by its step's accent, half as strongly as learned and against the part's own average in the bar (the
parts already lean on the beat). `--feeltest` checks it.
