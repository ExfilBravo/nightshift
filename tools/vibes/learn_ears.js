// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.
//
// The band's ears: every style the band plays (src/ai/Request.cpp's styles()), rendered as a whole song a few times
// (NightShift --bandrender ... --song --seed n) and heard by the vibe listener (NightShift --listen ... --features),
// its measures averaged and written to src/ai/StyleEars.cpp (from StyleEars.cpp.in) and ears.json (the same, to read).
//
// Usage, from the repository's top folder after a build:
//   node tools/vibes/learn_ears.js [path to the NightShift app] [--takes 3]
// It takes a few minutes: a render and a listen a take.

const fs = require('fs');
const os = require('os');
const path = require('path');
const { execFileSync } = require('child_process');

const repo = path.resolve(__dirname, '..', '..');
const args = process.argv.slice(2);
const takesAt = args.indexOf('--takes');
const takes = takesAt >= 0 ? parseInt(args[takesAt + 1], 10) : 3;
const given = args.filter((a, i) => !a.startsWith('--') && i !== takesAt + 1)[0];
const exe = given || (process.platform === 'win32'
    ? path.join(repo, 'build', 'Release', 'Nightshift_artefacts', 'Release', 'Nightshift.exe')
    : path.join(repo, 'build', 'Nightshift_artefacts', 'Release', 'Nightshift.app', 'Contents', 'MacOS', 'Nightshift'));

if (!fs.existsSync(exe)) {
    console.error(`No NightShift at ${exe}: build it, or give its path.`);
    process.exit(1);
}

// The styles: { "id", "Name", "family", slowest, usual, fastest, { "first word", ... } }
const source = fs.readFileSync(path.join(repo, 'src', 'ai', 'Request.cpp'), 'utf8');
const list = source.slice(source.indexOf('const std::vector<Style>& styles()'), source.indexOf('const Style* findStyle'));
const styles = [...list.matchAll(/\{\s*"(\w+)",\s*"([^"]+)",\s*"(\w+)",\s*(\d+),\s*(\d+),\s*(\d+),\s*\{\s*"([^"]+)"/g)]
    .map(m => ({ id: m[1], name: m[2], word: m[7] }));

if (styles.length === 0) {
    console.error('Found no styles in src/ai/Request.cpp.');
    process.exit(1);
}

const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'nightshift-ears-'));
const mean = (xs) => xs.reduce((a, b) => a + b, 0) / Math.max(1, xs.length);
const ears = [];

for (const s of styles) {
    const heard = [];

    for (let take = 1; take <= takes; ++take) {
        const wav = path.join(temp, `${s.id}-${take}.wav`);

        try {
            execFileSync(exe, ['--bandrender', wav, s.word, '--song', '--seed', String(take * 7 + 4)], { stdio: 'pipe' });
            const out = execFileSync(exe, ['--listen', wav, '--features'], { encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'] });
            const line = out.split(/\r?\n/).find(l => l.startsWith('features '));

            if (line)
                heard.push(JSON.parse(line.slice('features '.length)));
            else
                console.warn(`  ${s.id} take ${take}: not heard (${out.split(/\r?\n/)[0]})`);
        } catch (e) {
            console.warn(`  ${s.id} take ${take}: ${e.message.split('\n')[0]}`);
        }

        fs.rmSync(wav, { force: true });
    }

    if (heard.length === 0)
        continue;

    // The drums: each take's pattern (each band already scaled to its loudest), averaged, then scaled again
    const pattern = new Array(48).fill(0);

    for (const h of heard)
        h.pattern.forEach((x, i) => { pattern[i] += x / heard.length; });

    for (let band = 0; band < 3; ++band) {
        const top = Math.max(...pattern.slice(band * 16, band * 16 + 16));

        if (top > 0)
            for (let i = band * 16; i < band * 16 + 16; ++i)
                pattern[i] /= top;
    }

    const ear = {
        style: s.id,
        heard: heard.length,
        pattern: pattern.map(x => +x.toFixed(3)),
        tempo: +mean(heard.map(h => h.tempo)).toFixed(1),
        swing: +mean(heard.map(h => h.swing)).toFixed(3),
        highShare: +mean(heard.map(h => h.highShare)).toFixed(4),
        heldFlatness: +mean(heard.map(h => h.heldFlatness)).toFixed(4),
        lowShare: +mean(heard.map(h => h.lowShare)).toFixed(4),
        minorShare: +mean(heard.map(h => h.minorShare)).toFixed(3),
        seventhShare: +mean(heard.map(h => h.seventhShare)).toFixed(3),
        powerShare: +mean(heard.map(h => h.powerShare)).toFixed(3),
        minorKeys: +mean(heard.map(h => (h.mode === 'minor' ? 1 : 0))).toFixed(3),
    };

    ears.push(ear);
    console.log(`${s.id.padEnd(12)} ${heard.length} heard: ${ear.tempo} bpm, swing ${ear.swing}, high ${ear.highShare}, held flat ${ear.heldFlatness}, `
        + `low ${ear.lowShare}, minor ${ear.minorShare}, sevenths ${ear.seventhShare}, power ${ear.powerShare}, minor keys ${ear.minorKeys}`);
}

fs.rmSync(temp, { recursive: true, force: true });

const f = (x) => {
    const t = Number(x).toFixed(4).replace(/0+$/, '').replace(/\.$/, '.0');
    return `${t}f`;
};

const rows = ears.map(e => `        { "${e.style}", ${e.heard},\n`
    + `          { ${e.pattern.slice(0, 16).map(f).join(', ')},\n`
    + `            ${e.pattern.slice(16, 32).map(f).join(', ')},\n`
    + `            ${e.pattern.slice(32, 48).map(f).join(', ')} },\n`
    + `          ${f(e.tempo)}, ${f(e.swing)}, ${f(e.highShare)}, ${f(e.heldFlatness)}, ${f(e.lowShare)}, ${f(e.minorShare)}, ${f(e.seventhShare)}, `
    + `${f(e.powerShare)}, ${f(e.minorKeys)} },`).join('\n');

const template = fs.readFileSync(path.join(__dirname, 'StyleEars.cpp.in'), 'utf8');
fs.writeFileSync(path.join(repo, 'src', 'ai', 'StyleEars.cpp'), template.replace('/*EARS*/', rows));
fs.writeFileSync(path.join(__dirname, 'ears.json'), JSON.stringify(ears, null, 1) + '\n');
console.log(`Wrote src/ai/StyleEars.cpp and tools/vibes/ears.json: ${ears.length} styles, ${takes} takes each.`);
