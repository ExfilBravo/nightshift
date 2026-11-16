# The band's ears

`learn_ears.js` has the band play every style it knows and listens to it: each style (src/ai/Request.cpp's
`styles()`) rendered as a whole song three times by the whole band (`NightShift --bandrender <wav> "<style>" --song
--seed n`), each render heard by the vibe listener (`NightShift --listen <wav> --features`), and what it heard
averaged per style into `src/ai/StyleEars.cpp` (from `StyleEars.cpp.in`) and `ears.json` (the same numbers, to read).

When someone gives the band a song for its vibe (the AI tab's Song panel: Listen to a song...), the listener holds
what it hears up against these: the drums on each 16th of a bar (kick, snare, hats), how bright, gritty and heavy
below the sound is, how many of the chords are minor, sevenths or power chords, and the swing. That, with how near
the tempo is to where each style lives, says which of the band's styles the song is nearest.

Everything here is the band's own playing: no recordings of anyone else's, no download, nothing to credit.

## Running it

From the repository's top folder, after a Release build:

    node tools/vibes/learn_ears.js [path to the NightShift app] [--takes 3]

It takes about half an hour (a render of a couple of minutes' song and a listen, three times a style). Then build
again. Run it whenever the band's playing or the listener's measuring changes enough to matter: the listener's own
measures of a song should be compared with its own measures of the band, so the table and the listener go together.
The vibe test (`--vibetest`) checks there's an ear for every style.

## What it learns

Per style: the renders heard, the drum pattern (each band of 16 steps scaled to its loudest), the tempo and swing
heard, the share of the power over 2 kHz against everything over 200 Hz (brightness), how flat (noise-like) the held,
harmonic part of 1 to 6 kHz is (grit: distortion; the drums left out), the share under 120 Hz (low end), the
shares of the bars' chords that are minor, sevenths or power chords, and how many renders were heard in a minor key.
