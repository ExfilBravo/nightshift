// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#include "Genres.h"

namespace nightshift::agents
{

// Each genre as the band plays it: the style nearest it and the feel that makes it itself (the user,
// 2026-11-15: "I told it to make a sleep token style song and it gave me a blues shuffle"). Rows made
// from a table by tools that check every artist's genre is here and no name means two things.
const std::vector<Genre>& genres()
{
    static const std::vector<Genre> list {
        // id, name, plays, own, slowest, usual, fastest, feel, words, about
        { "altmetal", "Alternative metal", "djent", true, 70, 140, 180, "soft verses, heavy half-time chorus, drop c, synth",
          { "alternative metal", "alt metal", "alt-metal" },
          "heavy and dynamic: quiet verses, crushing half-time choruses, drop tuning" },
        { "progmetal", "Progressive metal", "djent", true, 90, 130, 180, "busy",
          { "progressive metal", "prog metal", "prog-metal", "prog" },
          "complex riffs and big turns, busy drums" },
        { "posthardcore", "Post-hardcore", "metalcore", true, 120, 160, 200, "soft verses, big chorus",
          { "post-hardcore", "post hardcore", "posthardcore", "screamo" },
          "urgent and melodic: driving verses, huge sung choruses" },
        { "groovemetal", "Groove metal", "metal", true, 90, 115, 140, "heavy",
          { "groove metal" },
          "mid-tempo and heavy: chugging riffs locked to the kick" },
        { "melodeath", "Melodic death metal", "thrash", true, 140, 180, 220, "busy",
          { "melodic death metal", "melodeath", "melo death", "gothenburg" },
          "fast and melodic: twin-guitar leads over a driving double kick" },
        { "doom", "Doom metal", "metal", true, 50, 66, 90, "half-time, heavy, drop a",
          { "doom", "doom metal", "stoner metal", "sludge", "sludge metal" },
          "slow and crushing: huge low riffs with room around every hit" },
        { "symphonic", "Symphonic metal", "metal", true, 100, 140, 180, "keys",
          { "symphonic metal", "symphonic" },
          "epic metal with orchestral keys" },
        { "industrial", "Industrial metal", "metal", true, 90, 120, 140, "synth, simple",
          { "industrial", "industrial metal" },
          "mechanical and heavy: tight straight beats, synths and chugs" },
        { "numetal", "Nu metal", "metal", true, 80, 100, 130, "heavy",
          { "nu metal", "nu-metal", "numetal", "rap metal" },
          "bouncy and heavy: low riffs, hip-hop-tinged grooves" },
        { "emo", "Emo", "poppunk", true, 120, 160, 200, "soft verses, big chorus",
          { "emo", "emo rock" },
          "heart-on-sleeve: driving and dynamic, big choruses" },
        { "midwestemo", "Midwest emo", "rock", true, 100, 140, 180, "busy, clean",
          { "midwest emo", "twinkly emo", "twinkle emo", "twinkle" },
          "twinkly clean guitars and busy, restless drums" },
        { "mathrock", "Math rock", "rock", true, 110, 150, 190, "busy, clean",
          { "math rock", "mathrock" },
          "intricate: tapped clean guitars, busy drums, odd turns" },
        { "postrock", "Post-rock", "rock", true, 60, 90, 130, "soft verses, builds, big chorus",
          { "post-rock", "post rock", "postrock" },
          "slow builds from quiet to huge, no hurry" },
        { "shoegaze", "Shoegaze", "rock", true, 70, 100, 140, "dreamy, synth",
          { "shoegaze", "dream pop", "dreampop", "blackgaze" },
          "washes of sound: dreamy and wide, soft and loud at once" },
        { "grunge", "Grunge", "rock", true, 90, 120, 150, "soft verses, heavy chorus, drop d",
          { "grunge", "post-grunge", "post grunge" },
          "the 90s quiet-loud: soft verses, distorted choruses" },
        { "blues", "Blues", "rock", true, 60, 90, 140, "shuffle, twelve bar, 12 bars",
          { "blues", "delta blues", "chicago blues", "twelve bar", "12 bar" },
          "a shuffle on twelve-bar changes" },
        { "bluesrock", "Blues rock", "hardrock", true, 80, 115, 150, "shuffle, twelve bar, 12 bars",
          { "blues rock", "blues-rock" },
          "driving rock on blues changes" },
        { "country", "Country", "country", true, 80, 110, 160, "simple",
          { "country", "americana", "honky tonk", "outlaw country" },
          "the train beat, a boom-chick bass, strummed open chords" },
        { "pop", "Pop", "rock", true, 90, 110, 130, "keys, synth, big chorus",
          { "pop", "pop rock", "power pop", "k-pop", "kpop", "j-pop" },
          "bright and catchy: a steady beat and big choruses" },
        { "emorap", "Emo rap", "trap", true, 120, 140, 170, "guitar",
          { "emo rap", "sad rap", "cloud rap" },
          "trap beats under sad guitar loops" },
        { "trapsoul", "Trapsoul", "trap", true, 120, 130, 150, "keys, soul chords, chill",
          { "trapsoul", "trap soul" },
          "slow trap beats with lush soul chords" },
        { "altrnb", "Alternative R&B", "neosoul", true, 60, 80, 100, "synth, dreamy",
          { "alternative r&b", "alt r&b", "alt rnb", "alt-r&b" },
          "moody, spacious R&B: lush chords and soft beats" },
        { "rnb", "R&B", "neosoul", true, 65, 85, 100, "keys",
          { "r&b", "rnb", "r and b", "contemporary r&b" },
          "smooth R&B grooves and rich chords" },
        { "soul", "Soul", "neosoul", true, 70, 95, 120, "keys",
          { "soul", "motown", "classic soul", "northern soul" },
          "warm and live: classic soul grooves and chords" },
        { "gospel", "Gospel", "neosoul", true, 70, 90, 130, "keys, big chorus",
          { "gospel", "church" },
          "rich gospel chords building to big choruses" },
        { "citypop", "City pop", "funk", true, 95, 110, 125, "keys, synth",
          { "city pop", "citypop", "japanese city pop" },
          "80s Tokyo: smooth funk under bright keys and synths" },
        { "disco", "Disco", "funk", true, 110, 120, 130, "four on the floor",
          { "disco", "nu disco", "nu-disco" },
          "four-on-the-floor dance grooves" },
        { "synthwave", "Synthwave", "synthwave", true, 80, 100, 125, "synth",
          { "synthwave", "retrowave", "outrun", "darksynth", "chillwave" },
          "80s neon: gated drums, octave bass in eighths, arpeggios and pads" },
        { "house", "House", "house", true, 118, 124, 128, "synth",
          { "house", "deep house", "tech house" },
          "four on the floor: the clap on 2 and 4, open hats between, an offbeat bass" },
        { "edm", "EDM", "edm", true, 120, 128, 130, "synth",
          { "edm", "electronic dance", "big room", "festival", "future bass" },
          "builds rolling up into big four-on-the-floor drops" },
        { "techno", "Techno", "techno", true, 125, 130, 140, "synth",
          { "techno" },
          "four on the floor, ticking hats, a rolling acid bass" },
        { "dubstep", "Dubstep", "dubstep", true, 135, 140, 150, "heavy, synth",
          { "dubstep", "brostep", "riddim" },
          "half-time drops: the snare on 3, a wobble bass" },
        { "dnb", "Drum and bass", "dnb", true, 160, 174, 180, "synth",
          { "dnb", "drum and bass", "drum & bass", "drum n bass", "jungle", "liquid" },
          "the two-step at 174, a long Reese bass" },
        { "ambient", "Ambient", "ambient", true, 50, 70, 90, "chill",
          { "ambient", "drone", "downtempo" },
          "no drums, long pads and a slow piano" },
        { "cinematic", "Cinematic", "cinematic", true, 60, 90, 120, "keys",
          { "cinematic", "film score", "orchestral", "epic score" },
          "big toms in 3+3+2, string pads, a piano ostinato" },
        { "reggae", "Reggae", "reggae", true, 60, 75, 90, "chill",
          { "reggae", "roots reggae", "dub", "ska" },
          "the one drop, a skank on 2 and 4, a deep bass" },
        { "reggaeton", "Reggaeton", "reggaeton", true, 85, 92, 100, "",
          { "reggaeton", "dembow", "latin trap" },
          "the dembow under an 808" },
        { "afrobeats", "Afrobeats", "afrobeats", true, 95, 105, 115, "",
          { "afrobeats", "afrobeat", "amapiano", "afropop" },
          "a syncopated kick, a shaker off the beat, highlife guitar" },
        { "bossa", "Bossa nova", "bossa", true, 110, 130, 150, "brushes, chill",
          { "bossa", "bossa nova", "samba", "latin jazz" },
          "the bossa clave, a dotted bass, seventh chords on the off-beats" },
    };

    return list;
}

const std::vector<Artist>& artists()
{
    static const std::vector<Artist> list {
        // name, genre, feel, other spellings
        { "Sleep Token", "altmetal", "piano, soul chords, ambient intro", { "sleeptoken" } },
        { "Bad Omens", "altmetal", "synth", {  } },
        { "Spiritbox", "altmetal", "synth, djent riffs", {  } },
        { "Deftones", "altmetal", "synth, dreamy", {  } },
        { "Loathe", "altmetal", "synth, dreamy", {  } },
        { "Bring Me The Horizon", "altmetal", "synth", { "bmth" } },
        { "Breaking Benjamin", "altmetal", "", {  } },
        { "Chevelle", "altmetal", "", {  } },
        { "Three Days Grace", "altmetal", "", {  } },
        { "Architects", "metalcore", "", {  } },
        { "Parkway Drive", "metalcore", "", {  } },
        { "Killswitch Engage", "metalcore", "", {  } },
        { "August Burns Red", "metalcore", "", {  } },
        { "As I Lay Dying", "metalcore", "", {  } },
        { "Wage War", "metalcore", "", {  } },
        { "Polaris", "metalcore", "", {  } },
        { "Currents", "metalcore", "", {  } },
        { "Erra", "metalcore", "", {  } },
        { "Of Mice & Men", "metalcore", "", { "of mice and men" } },
        { "Ice Nine Kills", "metalcore", "", {  } },
        { "Asking Alexandria", "metalcore", "", {  } },
        { "Motionless in White", "metalcore", "synth", {  } },
        { "I Prevail", "metalcore", "", {  } },
        { "Memphis May Fire", "metalcore", "", {  } },
        { "We Came As Romans", "metalcore", "", {  } },
        { "Crown the Empire", "metalcore", "", {  } },
        { "The Devil Wears Prada", "metalcore", "", {  } },
        { "Knocked Loose", "metalcore", "", {  } },
        { "Code Orange", "metalcore", "", {  } },
        { "Converge", "metalcore", "", {  } },
        { "The Dillinger Escape Plan", "metalcore", "busy", {  } },
        { "Northlane", "djent", "", {  } },
        { "Periphery", "djent", "", {  } },
        { "Meshuggah", "djent", "", {  } },
        { "TesseracT", "djent", "", {  } },
        { "Animals as Leaders", "djent", "", {  } },
        { "Monuments", "djent", "", {  } },
        { "Vildhjarta", "djent", "", {  } },
        { "Lorna Shore", "deathcore", "", {  } },
        { "Whitechapel", "deathcore", "", {  } },
        { "Suicide Silence", "deathcore", "", {  } },
        { "Thy Art Is Murder", "deathcore", "", {  } },
        { "Shadow of Intent", "deathcore", "", {  } },
        { "Chelsea Grin", "deathcore", "", {  } },
        { "Brand of Sacrifice", "deathcore", "", {  } },
        { "Polyphia", "progmetal", "clean", {  } },
        { "Intervals", "progmetal", "", {  } },
        { "Plini", "progmetal", "", {  } },
        { "Tool", "progmetal", "", {  } },
        { "Dream Theater", "progmetal", "", {  } },
        { "Opeth", "progmetal", "", {  } },
        { "Mastodon", "progmetal", "", {  } },
        { "Metallica", "thrash", "", {  } },
        { "Slayer", "thrash", "", {  } },
        { "Megadeth", "thrash", "", {  } },
        { "Anthrax", "thrash", "", {  } },
        { "Exodus", "thrash", "", {  } },
        { "Testament", "thrash", "", {  } },
        { "Kreator", "thrash", "", {  } },
        { "Power Trip", "thrash", "", {  } },
        { "Sepultura", "thrash", "", {  } },
        { "Pantera", "groovemetal", "", {  } },
        { "Lamb of God", "groovemetal", "", {  } },
        { "Machine Head", "groovemetal", "", {  } },
        { "Gojira", "groovemetal", "", {  } },
        { "Slipknot", "numetal", "", {  } },
        { "Korn", "numetal", "", {  } },
        { "Linkin Park", "numetal", "synth", {  } },
        { "Limp Bizkit", "numetal", "", {  } },
        { "System of a Down", "numetal", "", {  } },
        { "Disturbed", "numetal", "", {  } },
        { "Papa Roach", "numetal", "", {  } },
        { "Rage Against the Machine", "numetal", "", {  } },
        { "Iron Maiden", "metal", "gallops", {  } },
        { "Judas Priest", "metal", "", {  } },
        { "Ghost", "metal", "keys", {  } },
        { "Black Sabbath", "doom", "", {  } },
        { "Electric Wizard", "doom", "", {  } },
        { "Candlemass", "doom", "", {  } },
        { "Sleep", "doom", "", {  } },
        { "Cannibal Corpse", "blast", "", {  } },
        { "Death", "blast", "", {  } },
        { "Behemoth", "blast", "", {  } },
        { "Mayhem", "blast", "", {  } },
        { "Darkthrone", "blast", "", {  } },
        { "Emperor", "blast", "", {  } },
        { "Dimmu Borgir", "blast", "keys", {  } },
        { "Deafheaven", "blast", "dreamy", {  } },
        { "Nightwish", "symphonic", "", {  } },
        { "Epica", "symphonic", "", {  } },
        { "Within Temptation", "symphonic", "", {  } },
        { "Rammstein", "industrial", "", {  } },
        { "Nine Inch Nails", "industrial", "", { "nin" } },
        { "Ministry", "industrial", "", {  } },
        { "In Flames", "melodeath", "", {  } },
        { "Arch Enemy", "melodeath", "", {  } },
        { "At the Gates", "melodeath", "", {  } },
        { "Amon Amarth", "melodeath", "", {  } },
        { "Children of Bodom", "melodeath", "keys", {  } },
        { "Pierce the Veil", "posthardcore", "", {  } },
        { "Sleeping With Sirens", "posthardcore", "", {  } },
        { "Dance Gavin Dance", "posthardcore", "", {  } },
        { "Thrice", "posthardcore", "", {  } },
        { "Thursday", "posthardcore", "", {  } },
        { "Underoath", "posthardcore", "synth", {  } },
        { "Silverstein", "posthardcore", "", {  } },
        { "Senses Fail", "posthardcore", "", {  } },
        { "Hands Like Houses", "posthardcore", "", {  } },
        { "A Day to Remember", "poppunk", "with a breakdown", { "adtr" } },
        { "Blink-182", "poppunk", "", { "blink 182", "blink" } },
        { "Green Day", "poppunk", "", {  } },
        { "Sum 41", "poppunk", "", {  } },
        { "New Found Glory", "poppunk", "", {  } },
        { "The Story So Far", "poppunk", "", {  } },
        { "Neck Deep", "poppunk", "", {  } },
        { "State Champs", "poppunk", "", {  } },
        { "Paramore", "poppunk", "", {  } },
        { "Fall Out Boy", "poppunk", "", {  } },
        { "All Time Low", "poppunk", "", {  } },
        { "Simple Plan", "poppunk", "", {  } },
        { "Avril Lavigne", "poppunk", "", {  } },
        { "The Ramones", "punk", "", { "ramones" } },
        { "Sex Pistols", "punk", "", {  } },
        { "The Clash", "punk", "", {  } },
        { "Bad Religion", "punk", "", {  } },
        { "Rise Against", "punk", "", {  } },
        { "NOFX", "punk", "", {  } },
        { "Descendents", "punk", "", {  } },
        { "Black Flag", "punk", "", {  } },
        { "Dead Kennedys", "punk", "", {  } },
        { "Misfits", "punk", "", {  } },
        { "My Chemical Romance", "emo", "", { "mcr" } },
        { "Taking Back Sunday", "emo", "", {  } },
        { "Brand New", "emo", "", {  } },
        { "The Used", "emo", "", {  } },
        { "Dashboard Confessional", "emo", "", {  } },
        { "Jimmy Eat World", "emo", "", {  } },
        { "American Football", "midwestemo", "", {  } },
        { "Mineral", "midwestemo", "", {  } },
        { "Cap'n Jazz", "midwestemo", "", { "capn jazz" } },
        { "Sunny Day Real Estate", "midwestemo", "", {  } },
        { "Modern Baseball", "midwestemo", "", {  } },
        { "Tigers Jaw", "midwestemo", "", {  } },
        { "Origami Angel", "midwestemo", "", {  } },
        { "TTNG", "mathrock", "", { "this town needs guns" } },
        { "Covet", "mathrock", "", {  } },
        { "toe", "mathrock", "", {  } },
        { "Chon", "mathrock", "", {  } },
        { "Hella", "mathrock", "", {  } },
        { "Battles", "mathrock", "", {  } },
        { "Explosions in the Sky", "postrock", "", {  } },
        { "Godspeed You! Black Emperor", "postrock", "", { "godspeed you black emperor", "godspeed" } },
        { "Mogwai", "postrock", "", {  } },
        { "This Will Destroy You", "postrock", "", {  } },
        { "Sigur Ros", "postrock", "", { "sigur ros" } },
        { "Russian Circles", "postrock", "", {  } },
        { "Caspian", "postrock", "", {  } },
        { "God Is an Astronaut", "postrock", "", {  } },
        { "My Bloody Valentine", "shoegaze", "", {  } },
        { "Slowdive", "shoegaze", "", {  } },
        { "Ride", "shoegaze", "", {  } },
        { "Cocteau Twins", "shoegaze", "", {  } },
        { "Beach House", "shoegaze", "", {  } },
        { "Alcest", "shoegaze", "", {  } },
        { "Nirvana", "grunge", "", {  } },
        { "Pearl Jam", "grunge", "", {  } },
        { "Soundgarden", "grunge", "", {  } },
        { "Alice in Chains", "grunge", "", {  } },
        { "Stone Temple Pilots", "grunge", "", {  } },
        { "Smashing Pumpkins", "grunge", "", { "the smashing pumpkins" } },
        { "Foo Fighters", "rock", "", {  } },
        { "Radiohead", "rock", "", {  } },
        { "The Strokes", "rock", "", {  } },
        { "Arctic Monkeys", "rock", "", {  } },
        { "Muse", "rock", "", {  } },
        { "The White Stripes", "rock", "", {  } },
        { "Fleetwood Mac", "rock", "", {  } },
        { "Queen", "rock", "", {  } },
        { "Incubus", "rock", "", {  } },
        { "Red Hot Chili Peppers", "funk", "funk rock", { "rhcp" } },
        { "Queens of the Stone Age", "hardrock", "", { "qotsa" } },
        { "AC/DC", "hardrock", "", { "acdc", "ac dc" } },
        { "Led Zeppelin", "hardrock", "", {  } },
        { "Guns N' Roses", "hardrock", "", { "guns n roses", "guns and roses" } },
        { "Van Halen", "hardrock", "", {  } },
        { "Aerosmith", "hardrock", "", {  } },
        { "Deep Purple", "hardrock", "", {  } },
        { "Kiss", "hardrock", "", {  } },
        { "Def Leppard", "hardrock", "", {  } },
        { "Bon Jovi", "hardrock", "", {  } },
        { "Greta Van Fleet", "hardrock", "", {  } },
        { "Royal Blood", "hardrock", "", {  } },
        { "Shinedown", "hardrock", "", {  } },
        { "B.B. King", "blues", "", { "bb king", "b b king" } },
        { "Muddy Waters", "blues", "", {  } },
        { "Howlin' Wolf", "blues", "", { "howlin wolf" } },
        { "Robert Johnson", "blues", "", {  } },
        { "Buddy Guy", "blues", "", {  } },
        { "Albert King", "blues", "", {  } },
        { "Stevie Ray Vaughan", "bluesrock", "", { "srv" } },
        { "Gary Clark Jr.", "bluesrock", "", { "gary clark jr", "gary clark" } },
        { "Joe Bonamassa", "bluesrock", "", {  } },
        { "John Mayer", "bluesrock", "", {  } },
        { "Eric Clapton", "bluesrock", "", {  } },
        { "Jimi Hendrix", "bluesrock", "", { "hendrix" } },
        { "ZZ Top", "bluesrock", "", {  } },
        { "The Black Keys", "bluesrock", "", {  } },
        { "The Rolling Stones", "bluesrock", "", { "rolling stones" } },
        { "Johnny Cash", "country", "", {  } },
        { "Willie Nelson", "country", "", {  } },
        { "Dolly Parton", "country", "", {  } },
        { "Chris Stapleton", "country", "", {  } },
        { "Luke Combs", "country", "", {  } },
        { "Morgan Wallen", "country", "", {  } },
        { "Zach Bryan", "country", "", {  } },
        { "Tyler Childers", "country", "", {  } },
        { "Taylor Swift", "pop", "", {  } },
        { "Olivia Rodrigo", "pop", "", {  } },
        { "Harry Styles", "pop", "", {  } },
        { "Ed Sheeran", "pop", "", {  } },
        { "Billie Eilish", "pop", "", {  } },
        { "Lorde", "pop", "", {  } },
        { "Coldplay", "pop", "", {  } },
        { "Imagine Dragons", "pop", "", {  } },
        { "Maroon 5", "pop", "", {  } },
        { "Lady Gaga", "pop", "", {  } },
        { "The Beatles", "pop", "", { "beatles" } },
        { "Dua Lipa", "disco", "", {  } },
        { "Bruno Mars", "funk", "", {  } },
        { "Michael Jackson", "funk", "", {  } },
        { "Prince", "funk", "", {  } },
        { "Stevie Wonder", "funk", "", {  } },
        { "James Brown", "funk", "", {  } },
        { "Parliament", "funk", "", { "funkadelic", "parliament funkadelic" } },
        { "Earth, Wind & Fire", "funk", "", { "earth wind and fire", "earth wind & fire" } },
        { "Vulfpeck", "funk", "", {  } },
        { "Cory Wong", "funk", "", {  } },
        { "Snarky Puppy", "funk", "", {  } },
        { "Anderson .Paak", "funk", "", { "anderson paak" } },
        { "Childish Gambino", "funk", "", {  } },
        { "Chic", "disco", "", {  } },
        { "Bee Gees", "disco", "", {  } },
        { "Daft Punk", "disco", "", {  } },
        { "Tatsuro Yamashita", "citypop", "", {  } },
        { "Mariya Takeuchi", "citypop", "", {  } },
        { "Anri", "citypop", "", {  } },
        { "Travis Scott", "trap", "", {  } },
        { "Future", "trap", "", {  } },
        { "Metro Boomin", "trap", "", {  } },
        { "Playboi Carti", "trap", "rage", {  } },
        { "Lil Uzi Vert", "trap", "", {  } },
        { "21 Savage", "trap", "", {  } },
        { "Young Thug", "trap", "", {  } },
        { "Migos", "trap", "", {  } },
        { "Gunna", "trap", "", {  } },
        { "Lil Baby", "trap", "", {  } },
        { "$uicideboy$", "trap", "phonk", { "suicideboys", "suicideboy" } },
        { "Kordhell", "trap", "phonk", {  } },
        { "DVRST", "trap", "phonk", {  } },
        { "Pop Smoke", "drill", "", {  } },
        { "Central Cee", "drill", "", {  } },
        { "Headie One", "drill", "", {  } },
        { "Chief Keef", "drill", "", {  } },
        { "Fivio Foreign", "drill", "", {  } },
        { "Kanye West", "hiphop", "", { "kanye" } },
        { "Kendrick Lamar", "hiphop", "", { "kendrick" } },
        { "J. Cole", "hiphop", "", { "j cole" } },
        { "Drake", "hiphop", "", {  } },
        { "Eminem", "hiphop", "", {  } },
        { "2Pac", "hiphop", "", { "tupac", "2 pac" } },
        { "Nas", "boombap", "", {  } },
        { "Wu-Tang Clan", "boombap", "", { "wu tang", "wu-tang" } },
        { "The Notorious B.I.G.", "boombap", "", { "biggie", "notorious big", "biggie smalls" } },
        { "Mobb Deep", "boombap", "", {  } },
        { "A Tribe Called Quest", "boombap", "", { "tribe called quest", "atcq" } },
        { "J Dilla", "boombap", "swing", { "dilla" } },
        { "MF DOOM", "boombap", "", {  } },
        { "Joey Bada$", "boombap", "", { "joey badass" } },
        { "Freddie Gibbs", "boombap", "", {  } },
        { "Juice WRLD", "emorap", "", { "juice world" } },
        { "Lil Peep", "emorap", "", {  } },
        { "XXXTentacion", "emorap", "", {  } },
        { "Trippie Redd", "emorap", "", {  } },
        { "Nujabes", "lofi", "", {  } },
        { "Joji", "altrnb", "", {  } },
        { "Tom Misch", "neosoul", "", {  } },
        { "Erykah Badu", "neosoul", "", {  } },
        { "D'Angelo", "neosoul", "", { "dangelo" } },
        { "Hiatus Kaiyote", "neosoul", "", {  } },
        { "Thundercat", "neosoul", "", {  } },
        { "Robert Glasper", "neosoul", "", {  } },
        { "Daniel Caesar", "neosoul", "", {  } },
        { "H.E.R.", "neosoul", "", {  } },
        { "Steve Lacy", "neosoul", "", {  } },
        { "Frank Ocean", "altrnb", "", {  } },
        { "SZA", "altrnb", "", {  } },
        { "Brent Faiyaz", "altrnb", "", {  } },
        { "The Weeknd", "altrnb", "synth", { "weeknd" } },
        { "Summer Walker", "trapsoul", "", {  } },
        { "Bryson Tiller", "trapsoul", "", {  } },
        { "6LACK", "trapsoul", "", {  } },
        { "PARTYNEXTDOOR", "trapsoul", "", { "party next door" } },
        { "Ariana Grande", "rnb", "", {  } },
        { "Beyonce", "rnb", "", { "beyonce" } },
        { "Rihanna", "rnb", "", {  } },
        { "Miles Davis", "jazz", "", {  } },
        { "John Coltrane", "jazz", "", { "coltrane" } },
        { "Bill Evans", "jazz", "", {  } },
        { "Herbie Hancock", "jazz", "", {  } },
        { "Chet Baker", "ballad", "", {  } },
        { "Norah Jones", "ballad", "", {  } },
        { "Aretha Franklin", "soul", "", {  } },
        { "Marvin Gaye", "soul", "", {  } },
        { "Otis Redding", "soul", "", {  } },
        { "Al Green", "soul", "", {  } },
        { "Leon Bridges", "soul", "", {  } },
        { "Amy Winehouse", "soul", "", {  } },
        { "Adele", "soul", "", {  } },
        { "Kirk Franklin", "gospel", "", {  } },
        { "The Midnight", "synthwave", "", {  } },
        { "Kavinsky", "synthwave", "", {  } },
        { "Carpenter Brut", "synthwave", "heavy", {  } },
        { "Perturbator", "synthwave", "heavy", {  } },
        { "FM-84", "synthwave", "", { "fm84" } },
        { "Gunship", "synthwave", "", {  } },
        { "Timecop1983", "synthwave", "", {  } },
        { "Tycho", "synthwave", "chill", {  } },
        { "Skrillex", "dubstep", "", {  } },
        { "Avicii", "edm", "", {  } },
        { "Calvin Harris", "house", "", {  } },
        { "Fred again..", "house", "", {  } },
        { "Disclosure", "house", "", {  } },
        { "Deadmau5", "house", "", {  } },
        { "Pendulum", "dnb", "", {  } },
        { "Noisia", "dnb", "", {  } },
        { "Brian Eno", "ambient", "", {  } },
        { "Hans Zimmer", "cinematic", "", {  } },
        { "Bob Marley", "reggae", "", {  } },
        { "Bad Bunny", "reggaeton", "", {  } },
        { "Burna Boy", "afrobeats", "", {  } },
        { "Antonio Carlos Jobim", "bossa", "", { "antonio carlos jobim", "jobim", "tom jobim" } },
        { "Joao Gilberto", "bossa", "", { "joao gilberto" } },
    };

    return list;
}

const Genre* findGenre (const juce::String& id)
{
    for (auto& g : genres())
        if (id == g.id)
            return &g;

    return nullptr;
}

juce::String Named::feel() const
{
    juce::StringArray all;

    if (genre != nullptr)
        all.add (genre->feel);

    if (artist != nullptr)
        all.add (artist->feel);

    all.removeEmptyStrings();
    return all.joinIntoString (", ");
}

namespace
{
    // A letter without its accent (Beyonce is typed with an e acute or without; the names here are written without)
    juce_wchar folded (juce_wchar c)
    {
        // Capitals too: a Mac's towlower (its "C" locale) leaves an E acute a capital
        if (c >= 0xc0 && c <= 0xdd && c != 0xd7)
            c += 0x20;

        if (c >= 0xe0 && c <= 0xe5) return 'a';
        if (c == 0xe7) return 'c';
        if (c >= 0xe8 && c <= 0xeb) return 'e';
        if (c >= 0xec && c <= 0xef) return 'i';
        if (c == 0xf1) return 'n';
        if (c >= 0xf2 && c <= 0xf6) return 'o';
        if (c >= 0xf9 && c <= 0xfc) return 'u';
        if (c == 0xfd || c == 0xff) return 'y';
        return c;
    }

    juce::String foldedText (const juce::String& text)
    {
        juce::String s;
        s.preallocateBytes (text.getNumBytesAsUTF8() + 1);

        for (auto c : text)
            s += juce::String::charToString (folded (c));

        return s;
    }

    // As Request.cpp spaces words: lower case, anything but letters, digits, & / - # a space
    juce::String spacedName (const juce::String& name)
    {
        juce::String s;

        for (auto c : foldedText (name.toLowerCase()))
            s << (isWordLetter (c) || c == '/' || c == '&' || c == '-' || c == '#' ? juce::String::charToString (c) : juce::String (" "));

        return s.replace ("-", " - ").trim().replace ("  ", " ").replace ("  ", " ");
    }

    // A one-word name that's also a word ("Ghost", "Ride", "Future", "Death") only names an artist
    // beside a word that says it does: "like Ghost", "Ghost style", "a Future type beat", "Ride vibes"
    bool cued (const juce::String& text, int at, int length)
    {
        static const juce::StringArray before { "like", "by", "of", "la", "similar to", "inspired by" };
        static const juce::StringArray after { "style", "styled", "type", "vibe", "vibes", "song", "songs", "track", "tracks", "sound",
                                               "sounding", "inspired", "ish", "esque", "kind", "beat", "beats" };
        const auto left = text.substring (0, at).trimEnd().trimCharactersAtEnd ("- ").trimEnd();
        const auto right = text.substring (at + length).trimStart().trimCharactersAtStart ("- ").trimStart();

        for (auto& w : before)
            if (left.endsWith (" " + w) || left == w)
                return true;

        for (auto& w : after)
            if (right.startsWith (w + " ") || right == w)
                return true;

        return false;
    }
}

bool isWordLetter (juce::juce_wchar c)
{
    return juce::CharacterFunctions::isLetterOrDigit (c) || (c >= 0xc0 && c <= 0x24f && c != 0xd7 && c != 0xf7);
}

Named findNamed (const juce::String& spacedWords)
{
    const auto text = foldedText (spacedWords);
    Named best;

    auto consider = [&] (const juce::String& name, const Genre* genre, const Artist* artist)
    {
        const auto n = spacedName (name);

        if (n.isEmpty())
            return;

        // Every place it's named, the first that counts
        for (int at = text.indexOf (" " + n + " "); at >= 0; at = text.indexOf (at + 1, " " + n + " "))
        {
            const bool oneWord = n.containsOnly ("abcdefghijklmnopqrstuvwxyz");

            // A soul chord isn't soul music
            if (genre != nullptr && text.substring (at + n.length() + 2).startsWith ("chord"))
                continue;

            if (artist != nullptr && oneWord && ! cued (text, at + 1, n.length()))
                continue;

            // The longest name wins, the first of two as long; a genre or artist over a style's word
            // as long is the caller's choice
            if (n.length() > best.length || (n.length() == best.length && at < best.at))
            {
                best.genre = genre != nullptr ? genre : findGenre (artist->genre);
                best.artist = artist;
                best.at = at;
                best.length = n.length();
            }

            break;
        }
    };

    for (auto& g : genres())
        for (auto& w : g.words)
            consider (w, &g, nullptr);

    for (auto& a : artists())
    {
        consider (a.name, nullptr, &a);

        for (auto& w : a.words)
            consider (w, nullptr, &a);
    }

    return best;
}

juce::String withFeel (const juce::String& words)
{
    juce::String s;

    for (auto c : words.toLowerCase())
        s << (isWordLetter (c) || c == '/' || c == '&' || c == '-' || c == '#' ? juce::String::charToString (c) : juce::String (" "));

    const auto named = findNamed (" " + s.replace ("-", " - ").replace ("  ", " ") + " ");
    const auto feel = named.feel();
    return feel.isEmpty() ? words : words + ", " + feel;
}

} // namespace nightshift::agents
