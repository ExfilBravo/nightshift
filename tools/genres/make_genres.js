// Writes src/ai/Genres.cpp from this folder's tables: genres.tsv and artists.tsv into Genres.cpp.in. Checks as it
// goes: every artist's genre is a genre or one of the band's styles, no name or word means two things (compared
// as the parser sees them), and the result is ASCII (JUCE reads a char* as ASCII; accents are folded, and the
// parser folds what's typed the same way). Run after editing a table: node tools/genres/make_genres.js
const fs = require("fs"), path = require("path");
const here = __dirname;
// The band's styles, as Request.cpp's styles() lists them ({ "id", "Name", "family", slowest, usual, fastest, ...)
const styles = [...fs.readFileSync(path.join(here, "..", "..", "src", "ai", "Request.cpp"), "utf8")
                  .matchAll(/\{\s*"([a-z]+)",\s*"[^"]+",\s*"[a-z]+",\s*\d+,\s*\d+,\s*\d+,/g)].map(m => m[1]);

if (styles.length < 19)
    throw new Error("found only " + styles.length + " styles in Request.cpp");

const rows = f => fs.readFileSync(path.join(here, f), "utf8").split(/\r?\n/).filter(l => l.trim() && !l.startsWith("#")).map(l => l.split("\t"));
const ascii = s => {
    const t = String(s || "").normalize("NFD").replace(/[̀-ͯ]/g, "");
    if (/[^\x20-\x7e]/.test(t)) throw new Error("not ASCII after folding: " + s);
    return t;
};
const cpp = s => '"' + ascii(s).replace(/\\/g, "\\\\").replace(/"/g, '\\"') + '"';
const list = s => "{ " + String(s || "").split(",").map(w => w.trim()).filter(Boolean).map(cpp).join(", ") + " }";
const spaced = x => ascii(x).toLowerCase().replace(/[^a-z0-9&\/#\-]+/g, " ").trim().replace(/\s+/g, " ");

const ids = new Set(), words = new Map();
let genreRows = "        // id, name, plays, own, slowest, usual, fastest, feel, words, about\n";

for (const [id, name, plays, own, slowest, usual, fastest, feel, w, about] of rows("genres.tsv")) {
    if (ids.has(id)) throw new Error("genre twice: " + id);
    if (!styles.includes(plays)) throw new Error(`${id} plays "${plays}", not a style`);
    ids.add(id);
    for (const word of String(w).split(",").map(x => spaced(x.trim())).filter(Boolean)) {
        if (words.has(word)) throw new Error(`"${word}" names ${words.get(word)} and ${id}`);
        words.set(word, id);
    }
    genreRows += `        { ${cpp(id)}, ${cpp(name)}, ${cpp(plays)}, ${own === "yes" ? "true" : "false"}, ${+slowest}, ${+usual}, ${+fastest}, ${cpp(feel)},\n`
               + `          ${list(w)},\n          ${cpp(about)} },\n`;
}

const names = new Map();
let artistRows = "        // name, genre, feel, other spellings\n";
const artists = rows("artists.tsv");

for (const [name, genre, feel, also] of artists) {
    if (!ids.has(genre) && !styles.includes(genre)) throw new Error(`${name}: no genre or style "${genre}"`);
    for (const n of new Set([name, ...String(also || "").split(",")].map(x => spaced(x.trim())).filter(Boolean))) {
        if (names.has(n)) throw new Error(`"${n}" names ${names.get(n)} and ${name}`);
        if (words.has(n)) throw new Error(`"${n}" names the artist ${name} and the genre ${words.get(n)}`);
        names.set(n, name);
    }
    artistRows += `        { ${cpp(name)}, ${cpp(genre)}, ${cpp(feel)}, ${list(also)} },\n`;
}

const out = fs.readFileSync(path.join(here, "Genres.cpp.in"), "utf8").replace("@GENRES@", genreRows).replace("@ARTISTS@", artistRows);
if (/[^\x09\x0a\x0d\x20-\x7e]/.test(out)) throw new Error("Genres.cpp would have a character that isn't ASCII");
fs.writeFileSync(path.join(here, "..", "..", "src", "ai", "Genres.cpp"), out);
console.log(`src/ai/Genres.cpp: ${ids.size} genres, ${artists.length} artists, ${names.size} artist names`);
