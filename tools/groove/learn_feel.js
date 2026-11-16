// Learns how real drummers play each style from Google Magenta's Groove MIDI Dataset (CC BY 4.0; see README.md here)
// and writes src/ai/LearnedFeel.cpp (from LearnedFeel.cpp.in) and learned.json (the same, to read).
//
// For every recorded beat (not the fills) in 4/4 of the dataset styles a style or genre learns from: each hit placed on
// the nearest 16th (a triplet feel's on the nearest 8th-note triplet), how far off it is in ms, each bar's drift against
// the click taken out (the median of its on-beat hits), and from what's left, per style:
//   swing        how late the offbeat 16ths (or 8ths) are, as Groove's swing (0 straight .. 1 a full triplet)
//   push         each drum's average ms ahead (-) or behind (+), after the swing
//   timing       each drum, each step of the bar: its average ms against that push (where it's played often enough)
//   accent       each drum, each step: its average velocity against the drum's own average
//   looseness    what's left (each hit against its step's average): Groove's looseness_ms
//
// Usage: node tools/groove/learn_feel.js <the dataset's "groove" folder (holding info.csv)>
const fs = require("fs"), path = require("path");
const here = __dirname, root = path.join(here, "..", "..");
const data = process.argv[2];

if (!data || !fs.existsSync(path.join(data, "info.csv")))
    throw new Error("usage: node tools/groove/learn_feel.js <the Groove MIDI Dataset's groove folder, holding info.csv>");

//==============================================================================
// What each style (or genre) learns from: the dataset's styles (its style column, before the groove number), and
// whether NightShift swings it in 16ths. A style the dataset has nothing of plays its own feel, as do the programmed
// ones (trap, drill, electronic, synthwave, reggaeton) that are tight on purpose, and lofi's and neo-soul's
// deliberately lazy pocket. "keep": what a style keeps of its own (boom bap its MPC swing and lazy snare: the dataset's
// hip-hop drummers barely swing; reggae's and afrobeats' swing: the dataset's few performances are mostly straight).
const learners = [
    { id: "rock",      kind: "style", say: "rock drummers",        from: ["rock", "rock/groove", "rock/indie", "rock/prog", "rock/halftime"], grid: 16, sixteenths: false },
    { id: "hardrock",  kind: "style", say: "rock drummers",        from: ["rock", "rock/groove", "rock/indie", "rock/prog", "rock/halftime"], grid: 16, sixteenths: false },
    { id: "punk",      kind: "style", say: "punk drummers",        from: ["punk"], grid: 16, sixteenths: false },
    { id: "poppunk",   kind: "style", say: "punk drummers",        from: ["punk"], grid: 16, sixteenths: false },
    { id: "funk",      kind: "style", say: "funk drummers",        from: ["funk", "funk/groove", "funk/fast", "funk/rock", "funk/latin", "neworleans/funk"], grid: 16, sixteenths: true },
    { id: "hiphop",    kind: "style", say: "hip-hop drummers",     from: ["hiphop", "hiphop/groove"], grid: 16, sixteenths: true },
    { id: "boombap",   kind: "style", say: "hip-hop drummers",     from: ["hiphop", "hiphop/groove"], grid: 16, sixteenths: true, keep: ["swing", "push"] },
    { id: "jazz",      kind: "style", say: "jazz drummers",        from: ["jazz", "jazz/swing", "jazz/mediumfast", "jazz/fast"], grid: 12, sixteenths: false },
    { id: "ballad",    kind: "style", say: "jazz drummers",        from: ["jazz", "jazz/swing", "jazz/mediumfast", "jazz/fast"], grid: 12, sixteenths: false },
    { id: "reggae",    kind: "style", say: "reggae drummers",      from: ["reggae", "reggae/slow"], grid: 16, sixteenths: false, keep: ["swing"] },
    { id: "afrobeats", kind: "style", say: "afrobeat and highlife drummers", from: ["afrobeat", "highlife"], grid: 16, sixteenths: true, keep: ["swing"] },
    { id: "bossa",     kind: "style", say: "bossa nova drummers",  from: ["latin/brazilian-bossa"], grid: 16, sixteenths: false },
    { id: "country",   kind: "style", say: "country and folk drummers", from: ["country", "rock/folk", "rock/rockabilly"], grid: 16, sixteenths: false },
    { id: "pop",       kind: "genre", say: "pop drummers",         from: ["pop", "pop/groove", "pop/soft"], grid: 16, sixteenths: false },
    { id: "soul",      kind: "genre", say: "soul drummers",        from: ["soul", "soul/groove", "soul/motown"], grid: 16, sixteenths: true },
    { id: "gospel",    kind: "genre", say: "gospel and soul drummers", from: ["gospel", "soul", "soul/groove", "soul/motown"], grid: 16, sixteenths: true },
    { id: "disco",     kind: "genre", say: "disco drummers",       from: ["dance/disco"], grid: 16, sixteenths: true },
];

// The kit's notes (the dataset's Roland TD-11 mapping, its page's table) as the drums a feel is kept for
const drumOf = new Map([
    [36, "kick"], [38, "snare"], [40, "snare"], [37, "sidestick"],
    [42, "hat_closed"], [22, "hat_closed"], [44, "hat_pedal"], [46, "hat_open"], [26, "hat_open"],
    [51, "ride"], [59, "ride"], [53, "ride"], [49, "crash"], [55, "crash"], [57, "crash"], [52, "crash"],
    [48, "toms"], [50, "toms"], [45, "toms"], [47, "toms"], [43, "toms"], [58, "toms"],
]);
const drums = ["kick", "snare", "sidestick", "hat_closed", "hat_pedal", "hat_open", "ride", "crash", "toms"];

const minutesAtLeast = 2;           // less than this of playing, and a style keeps its own feel
const hitsAtLeast = 12;             // a step a drum plays fewer times than this keeps the drum's push and accent

//==============================================================================
// A Standard MIDI File's note-ons (time in ticks) and its ticks a beat
function readMidi(file) {
    const buf = fs.readFileSync(file);
    let p = 0;
    const tag = () => buf.toString("latin1", p, p + 4);
    const vlq = () => { let v = 0, b; do { b = buf[p++]; v = (v << 7) | (b & 0x7f); } while (b & 0x80); return v; };

    if (tag() !== "MThd") throw new Error("not a MIDI file: " + file);
    const headerLength = buf.readUInt32BE(4), tracks = buf.readUInt16BE(10), division = buf.readUInt16BE(12);
    if (division & 0x8000) throw new Error("SMPTE time, not ticks a beat: " + file);
    p = 8 + headerLength;
    const notes = [];

    for (let t = 0; t < tracks && p + 8 <= buf.length; t++) {
        const id = tag(), length = buf.readUInt32BE(p + 4);
        p += 8;
        const end = p + length;
        if (id !== "MTrk") { p = end; continue; }
        let tick = 0, status = 0;

        while (p < end) {
            tick += vlq();
            const s = buf[p];

            // Meta and sysex events skipped (their length read first: "p += vlq()" would add it to p from before
            // the length's own bytes)
            if (s === 0xff) { p += 2; const length = vlq(); p += length; continue; }
            if (s === 0xf0 || s === 0xf7) { p++; const length = vlq(); p += length; continue; }
            if (s & 0x80) { status = s; p++; }                           // else running status

            const type = status & 0xf0, d1 = buf[p++], d2 = type === 0xc0 || type === 0xd0 ? 0 : buf[p++];

            if (type === 0x90 && d2 > 0)
                notes.push({ tick, pitch: d1, velocity: d2 });
        }

        p = end;
    }

    return { notes, division };
}

const median = v => { if (!v.length) return 0; const s = [...v].sort((a, b) => a - b), m = s.length >> 1; return s.length % 2 ? s[m] : (s[m - 1] + s[m]) / 2; };
const mean = v => v.length ? v.reduce((a, b) => a + b, 0) / v.length : 0;
const sd = v => { const m = mean(v); return Math.sqrt(mean(v.map(x => (x - m) * (x - m)))); };
const round = (x, places) => Number(x.toFixed(places));

//==============================================================================
// Each beat's hits: where in the bar (a step of 16 or 12), how far off in ms, its bar, its drum and velocity. The feel
// of the whole performance: does it sit on 8th-note triplets (a swing or a shuffle) or on 16ths?
function hitsOf(row) {
    const { notes, division } = readMidi(path.join(data, row.midi_filename));
    const bpm = +row.bpm, msPerBeat = 60000 / bpm;
    const beats = notes.map(n => ({ beat: n.tick / division, drum: drumOf.get(n.pitch), velocity: n.velocity })).filter(n => n.drum);

    // Triplet or 16ths: where the offbeat hits fall in their beat
    let triplet = 0, sixteenth = 0;

    for (const n of beats) {
        const f = n.beat - Math.floor(n.beat);
        if (Math.abs(f - 1 / 3) < 0.05 || Math.abs(f - 2 / 3) < 0.05) triplet++;
        if (Math.abs(f - 0.25) < 0.04 || Math.abs(f - 0.75) < 0.04) sixteenth++;
    }

    const grid = triplet > 1.5 * sixteenth && triplet > 0.08 * beats.length ? 12 : 16;
    const per = grid / 4;                                   // steps a beat
    const msPerStep = msPerBeat / per;
    const place = shift => beats.map(n => {
        const x = (n.beat - shift) * per, at = Math.round(x);
        return { ...n, step: ((at % grid) + grid) % grid, bar: Math.floor(at / grid), ms: (x - at) * msPerStep };
    });

    // A recording that starts off the click by more than a little: line it up first (its on-beat hits' median)
    let hits = place(0);
    const off = median(hits.filter(h => h.step % per === 0).map(h => h.ms));
    if (Math.abs(off) > msPerStep * 0.15)
        hits = place(off / msPerStep / per);

    // Each bar's drift against the click (rushing, dragging) out: the median of its on-beat hits
    const bars = new Map();
    for (const h of hits) if (h.step % per === 0) (bars.get(h.bar) || bars.set(h.bar, []).get(h.bar)).push(h.ms);
    const whole = median(hits.filter(h => h.step % per === 0).map(h => h.ms));
    for (const h of hits) { const b = bars.get(h.bar); h.ms -= b && b.length >= 2 ? median(b) : whole; }

    // A flam's or a drag's grace notes: a hit with a louder one of the same drum just after it (inside 60 ms). They
    // aren't timing, and they'd pull the drum's average early. And the kit's crosstalk: a very soft hit (under 20) with
    // a much louder one on another drum inside 25 ms (the kick shaking the snare's pad: punk's "snare" on every kick)
    const byTime = [...hits].sort((a, b) => a.beat - b.beat);
    for (let i = 0; i < byTime.length; i++)
        for (let j = i + 1; j < byTime.length && (byTime[j].beat - byTime[i].beat) * msPerBeat < 60; j++)
            if (byTime[j].drum === byTime[i].drum && byTime[j].velocity > byTime[i].velocity)
                byTime[i].grace = true;

    for (let i = 0; i < byTime.length; i++)
        if (byTime[i].velocity < 20)
            for (let j = Math.max(0, i - 8); j < Math.min(byTime.length, i + 9); j++)
                if (j !== i && byTime[j].drum !== byTime[i].drum && byTime[j].velocity >= 2 * byTime[i].velocity
                    && Math.abs(byTime[j].beat - byTime[i].beat) * msPerBeat < 25)
                    byTime[i].grace = true;

    return { grid, hits, seconds: +row.duration, msPerStep };
}

//==============================================================================
const csv = fs.readFileSync(path.join(data, "info.csv"), "utf8").split(/\r?\n/).filter(Boolean);
const head = csv[0].split(",");
const rows = csv.slice(1).map(l => Object.fromEntries(l.split(",").map((v, i) => [head[i], v])));
const styleOf = row => row.style.replace(/\d+$/, "");
const learned = [];

for (const L of learners) {
    const chosen = rows.filter(r => r.beat_type === "beat" && r.time_signature === "4-4" && L.from.includes(styleOf(r)));
    const takes = chosen.map(r => ({ row: r, ...hitsOf(r) })).filter(t => t.grid === L.grid);
    const seconds = takes.reduce((a, t) => a + t.seconds, 0);

    if (seconds < minutesAtLeast * 60) {
        console.log(`${L.id}: ${takes.length} beats, ${(seconds / 60).toFixed(1)} min on a ${L.grid}-step grid: too little, keeps its own feel`);
        continue;
    }

    const per = L.grid / 4;

    // Swing first: how much later the offbeat 16ths (or 8ths) land than the beats, as Groove plays it (16ths: a full
    // swing puts them a third of a 16th late; 8ths: two thirds of a 16th), in steps, from the middle of each (medians,
    // within 45% of a step: a heavy swing is further off than the window below allows). A triplet grid has none
    const near = takes.flatMap(t => t.hits.filter(h => !h.grace && Math.abs(h.ms) <= 0.45 * t.msPerStep).map(h => ({ h, t })));
    const stepsOff = pick => median(near.filter(({ h }) => pick(h)).map(({ h, t }) => h.ms / t.msPerStep));
    let swing = 0;

    if (L.grid === 16) {
        const onBeat = stepsOff(h => h.step % 4 === 0);
        swing = L.sixteenths ? 3 * (stepsOff(h => h.step % 2 === 1) - onBeat) : 1.5 * (stepsOff(h => h.step % 4 === 2) - onBeat);
        swing = Math.min(1, Math.max(0, swing));
    }

    // What the swing moves each step, taken out (in ms at the hit's own take's tempo); then only what's played on the
    // grid counts: no grace notes, and nothing further from its (swung) step than 30% of a step (a 32nd, a triplet in
    // a 16th groove, a fill's figure: another subdivision, not a feel)
    const swingSteps = step => L.grid !== 16 ? 0 : L.sixteenths ? (step % 2 === 1 ? swing / 3 : 0) : (step % 4 === 2 ? swing * 2 / 3 : 0);

    for (const t of takes)
        for (const h of t.hits) {
            h.take = t;
            h.rest = h.ms - swingSteps(h.step) * t.msPerStep;
            h.on = !h.grace && Math.abs(h.rest) <= 0.3 * t.msPerStep;
        }

    const hits = takes.flatMap(t => t.hits.filter(h => h.on));

    const feel = { id: L.id, kind: L.kind, from: L.say, styles: L.from, grooves: takes.length, minutes: Math.round(seconds / 60),
                   steps: L.grid, swing: round(swing, 2), sixteenths: L.sixteenths, keeps: L.keep || [], push: {}, timing: {}, accent: {}, hits: {} };

    for (const drum of drums) {
        const mine = hits.filter(h => h.drum === drum);
        if (mine.length < 3 * hitsAtLeast) continue;

        const push = mean(mine.map(h => h.rest));
        const velocity = mean(mine.map(h => h.velocity));
        const timing = [], accent = [], counts = [];
        const busiest = Math.max(...Array.from({ length: L.grid }, (_, step) => mine.filter(h => h.step === step).length));

        // A step's own timing and accent where the drum plays it often (as often as a fifth of its busiest step): where
        // these drummers hardly did, what little there is says nothing of a part that does. Accents kept to 0.5..1.6
        for (let step = 0; step < L.grid; step++) {
            const here = mine.filter(h => h.step === step);
            counts.push(here.length);
            const enough = here.length >= hitsAtLeast && here.length >= 0.2 * busiest;
            timing.push(enough ? round(mean(here.map(h => h.rest)) - push, 1) : 0);
            accent.push(enough ? round(Math.min(1.6, Math.max(0.5, mean(here.map(h => h.velocity)) / velocity)), 2) : 1);

            for (const h of here)
                h.left = h.rest - push - timing[step];
        }

        feel.push[drum] = round(push, 1);
        feel.timing[drum] = timing;
        feel.accent[drum] = accent;
        feel.hits[drum] = counts;
    }

    // What's left once each beat's shared lean is out too (the hands and feet move together: a beat early is early on
    // every drum, and NightShift's wander is each hit's own), as a robust spread (1.4826 times the median distance),
    // then as Groove's looseness_ms (its wander's spread is 0.3 times that: three uniforms less 1.5, times 0.6)
    const beatsOf = new Map();
    for (const h of hits.filter(h => h.left !== undefined)) {
        const key = `${h.take.row.id}|${h.bar}|${Math.floor(h.step / per)}`;
        (beatsOf.get(key) || beatsOf.set(key, []).get(key)).push(h);
    }

    const own = [];
    for (const group of beatsOf.values()) {
        const shared = group.length >= 2 ? median(group.map(h => h.left)) : 0;
        for (const h of group) own.push(h.left - shared);
    }

    const middle = median(own);
    const spread = 1.4826 * median(own.map(x => Math.abs(x - middle)));
    feel.spreadMs = round(spread, 1);
    feel.looseMs = round(Math.min(30, spread / 0.3), 1);
    learned.push(feel);

    const pushes = Object.entries(feel.push).map(([d, ms]) => `${d} ${ms > 0 ? "+" : ""}${ms}`).join(", ");
    console.log(`${L.id}: ${feel.grooves} beats, ${feel.minutes} min, ${L.grid} steps, swing ${feel.swing}${L.grid === 16 ? (L.sixteenths ? " (16ths)" : " (8ths)") : ""}, `
                + `spread ${feel.spreadMs} ms (looseness ${feel.looseMs}); push: ${pushes}`);
}

fs.writeFileSync(path.join(here, "learned.json"), JSON.stringify({ source: "Groove MIDI Dataset v1.0.0 (Google Magenta, CC BY 4.0)", feels: learned }, null, 1) + "\n");

//==============================================================================
// The C++ table
const num = x => (Number.isInteger(x) ? x.toFixed(1) : String(x)) + "f";
const arr = v => "{ " + v.map(num).join(", ") + " }";
let table = "";

for (const f of learned) {
    // (a style that keeps its own swing has -1 there, its own push an empty list)
    const swing = f.keeps.includes("swing") ? "-1.0f" : num(f.swing);
    const push = f.keeps.includes("push") ? "" : Object.entries(f.push).map(([d, ms]) => `{ "${d}", ${num(ms)} }`).join(", ");
    table += `        { "${f.id}", "${f.from}", ${f.grooves}, ${f.minutes}, ${f.steps}, ${swing}, ${f.sixteenths}, ${num(f.looseMs)},\n`;
    table += `          { ${push} },\n`;
    table += `          {\n${Object.entries(f.timing).map(([d, v]) => `              { "${d}", ${arr(v)} }`).join(",\n")} },\n`;
    table += `          {\n${Object.entries(f.accent).map(([d, v]) => `              { "${d}", ${arr(v)} }`).join(",\n")} } },\n`;
}

const template = fs.readFileSync(path.join(here, "LearnedFeel.cpp.in"), "utf8");
if (!template.includes("@@FEELS@@")) throw new Error("LearnedFeel.cpp.in has no @@FEELS@@");
fs.writeFileSync(path.join(root, "src", "ai", "LearnedFeel.cpp"), template.replace("@@FEELS@@", table.trimEnd()));
console.log(`wrote src/ai/LearnedFeel.cpp: ${learned.length} feels`);
