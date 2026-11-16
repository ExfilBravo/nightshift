// Per-channel look at a (multichannel) WAV: peak, and the level in each band over the attack
// (first 30 ms) and over the whole hit, in dB. For checking how a drum's mics and a mixdown sound.
//   node tools/mics.mjs hit.wav [names,comma,separated]
import fs from 'node:fs';

function readWav(file) {
  const buf = fs.readFileSync(file);
  let fmt = null, dataOffset = -1, dataSize = 0, pos = 12;

  while (pos + 8 <= buf.length) {
    const id = buf.toString('ascii', pos, pos + 4);
    const size = buf.readUInt32LE(pos + 4);
    if (id === 'fmt ') {
      fmt = { format: buf.readUInt16LE(pos + 8), channels: buf.readUInt16LE(pos + 10), sampleRate: buf.readUInt32LE(pos + 12), bits: buf.readUInt16LE(pos + 22) };
      if (fmt.format === 0xFFFE) fmt.format = buf.readUInt16LE(pos + 32);
    } else if (id === 'data') {
      dataOffset = pos + 8;
      dataSize = Math.min(size, buf.length - dataOffset);
    }
    pos += 8 + size + (size & 1);
  }

  const bytes = fmt.bits / 8, frames = Math.floor(dataSize / (bytes * fmt.channels));
  const channels = Array.from({ length: fmt.channels }, () => new Float32Array(frames));

  for (let i = 0; i < frames; i++)
    for (let c = 0; c < fmt.channels; c++) {
      const o = dataOffset + (i * fmt.channels + c) * bytes;
      channels[c][i] = fmt.format === 3 ? buf.readFloatLE(o) : bytes === 2 ? buf.readInt16LE(o) / 32768 : bytes === 3 ? buf.readIntLE(o, 3) / 8388608 : buf.readInt32LE(o) / 2147483648;
    }

  return { sampleRate: fmt.sampleRate, channels };
}

function fft(re, im) {
  const n = re.length;
  for (let i = 1, j = 0; i < n; i++) {
    let bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) { [re[i], re[j]] = [re[j], re[i]]; [im[i], im[j]] = [im[j], im[i]]; }
  }
  for (let len = 2; len <= n; len <<= 1) {
    const ang = -2 * Math.PI / len, wr = Math.cos(ang), wi = Math.sin(ang);
    for (let i = 0; i < n; i += len) {
      let cr = 1, ci = 0;
      for (let k = 0; k < len / 2; k++) {
        const ar = re[i + k], ai = im[i + k], br = re[i + k + len / 2] * cr - im[i + k + len / 2] * ci, bi = re[i + k + len / 2] * ci + im[i + k + len / 2] * cr;
        re[i + k] = ar + br; im[i + k] = ai + bi; re[i + k + len / 2] = ar - br; im[i + k + len / 2] = ai - bi;
        const t = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = t;
      }
    }
  }
}

const bands = [['sub', 20, 60], ['bass', 60, 250], ['lowmid', 250, 1000], ['mid', 1000, 4000], ['high', 4000, 10000], ['air', 10000, 20000]];

function bandLevels(x, fs, from, count) {
  let n = 1;
  while (n < count) n <<= 1;
  const re = new Float64Array(n), im = new Float64Array(n);
  for (let i = 0; i < count && from + i < x.length; i++) re[i] = x[from + i] * (0.5 - 0.5 * Math.cos(2 * Math.PI * i / (count - 1)));
  fft(re, im);
  return bands.map(([, lo, hi]) => {
    let e = 0;
    for (let k = Math.ceil(lo * n / fs); k <= Math.floor(hi * n / fs) && k < n / 2; k++) e += re[k] * re[k] + im[k] * im[k];
    return e > 0 ? (10 * Math.log10(e)).toFixed(0).padStart(4) : '-inf';
  }).join(' ');
}

const [file, names] = process.argv.slice(2);
const { sampleRate: fs_, channels } = readWav(file);
const labels = names ? names.split(',') : channels.map((_, i) => `ch${i + 1}`);
console.log(`${file}: ${channels.length} ch, ${fs_} Hz, ${(channels[0].length / fs_).toFixed(2)} s`);
console.log(`${'channel'.padEnd(14)} peak   | attack 30 ms: ${bands.map(b => b[0].padStart(4)).join(' ')} | whole: ${bands.map(b => b[0].padStart(4)).join(' ')}`);

channels.forEach((x, c) => {
  let peak = 0;
  for (const v of x) peak = Math.max(peak, Math.abs(v));
  const attack = bandLevels(x, fs_, 0, Math.round(fs_ * 0.03));
  const whole = bandLevels(x, fs_, 0, Math.min(x.length, 1 << 17));
  console.log(`${(labels[c] || `ch${c + 1}`).padEnd(14)} ${(20 * Math.log10(peak || 1e-9)).toFixed(1).padStart(6)} |              ${attack} |        ${whole}`);
});
