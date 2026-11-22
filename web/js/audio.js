// Output: WAV / MP3 encoding, ZIP packaging, playback.
import { resample } from './dsp.js';

const toS16 = (v) => Math.max(-32768, Math.min(32767, Math.round(v * 32767)));

// 8-bit unsigned PCM when eightBit and rate == 11025 (the game's format), else 16-bit.
export function makeWav(x, rate, eightBit) {
    const u8 = eightBit && rate === 11025;
    const bps = u8 ? 1 : 2, dataLen = x.length * bps, pad = dataLen & 1;
    const buf = new ArrayBuffer(44 + dataLen + pad);
    const dv = new DataView(buf);
    const str = (o, s) => { for (let i = 0; i < s.length; i++) dv.setUint8(o + i, s.charCodeAt(i)); };
    str(0, 'RIFF'); dv.setUint32(4, 36 + dataLen + pad, true); str(8, 'WAVEfmt ');
    dv.setUint32(16, 16, true); dv.setUint16(20, 1, true); dv.setUint16(22, 1, true);
    dv.setUint32(24, rate, true); dv.setUint32(28, rate * bps, true); dv.setUint16(32, bps, true); dv.setUint16(34, bps * 8, true);
    str(36, 'data'); dv.setUint32(40, dataLen, true);
    if (u8) for (let i = 0; i < x.length; i++) dv.setUint8(44 + i, Math.max(0, Math.min(255, Math.round(x[i] * 127) + 128)));
    else for (let i = 0; i < x.length; i++) dv.setInt16(44 + i * 2, toS16(x[i]), true);
    return new Uint8Array(buf);
}

export function makeMp3(x, rate) {
    const lame = globalThis.lamejs;
    if (!lame) throw new Error('MP3 encoder not loaded');
    const enc = new lame.Mp3Encoder(1, rate, rate <= 11025 ? 32 : 128);
    const pcm = new Int16Array(x.length);
    for (let i = 0; i < x.length; i++) pcm[i] = toS16(x[i]);
    const chunks = [];
    for (let i = 0; i < pcm.length; i += 1152 * 16) chunks.push(enc.encodeBuffer(pcm.subarray(i, i + 1152 * 16)));
    chunks.push(enc.flush());
    let n = 0;
    for (const c of chunks) n += c.length;
    const out = new Uint8Array(n);
    let o = 0;
    for (const c of chunks) { out.set(c, o); o += c.length; }
    return out;
}

// ---- minimal ZIP (stored) ----
let CRC;
function crc32(d) {
    if (!CRC) {
        CRC = new Uint32Array(256);
        for (let i = 0; i < 256; i++) { let c = i; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; CRC[i] = c >>> 0; }
    }
    let c = 0xFFFFFFFF;
    for (let i = 0; i < d.length; i++) c = CRC[(c ^ d[i]) & 255] ^ (c >>> 8);
    return (c ^ 0xFFFFFFFF) >>> 0;
}
export function makeZip(files) {
    const enc = new TextEncoder();
    const local = [], central = [];
    let offset = 0;
    for (const f of files) {
        const name = enc.encode(f.name), crc = crc32(f.data), size = f.data.length;
        const h = new DataView(new ArrayBuffer(30));
        h.setUint32(0, 0x04034b50, true); h.setUint16(4, 20, true); h.setUint16(8, 0, true);
        h.setUint16(12, 0x21, true);  // DOS date 1980-01-01
        h.setUint32(14, crc, true); h.setUint32(18, size, true); h.setUint32(22, size, true); h.setUint16(26, name.length, true);
        local.push(new Uint8Array(h.buffer), name, f.data);
        const c = new DataView(new ArrayBuffer(46));
        c.setUint32(0, 0x02014b50, true); c.setUint16(4, 20, true); c.setUint16(6, 20, true);
        c.setUint16(14, 0x21, true);
        c.setUint32(16, crc, true); c.setUint32(20, size, true); c.setUint32(24, size, true); c.setUint16(28, name.length, true);
        c.setUint32(42, offset, true);
        central.push(new Uint8Array(c.buffer), name);
        offset += 30 + name.length + size;
    }
    let csize = 0;
    for (const p of central) csize += p.length;
    const end = new DataView(new ArrayBuffer(22));
    end.setUint32(0, 0x06054b50, true); end.setUint16(8, files.length, true); end.setUint16(10, files.length, true);
    end.setUint32(12, csize, true); end.setUint32(16, offset, true);
    const parts = [...local, ...central, new Uint8Array(end.buffer)];
    let n = 0;
    for (const p of parts) n += p.length;
    const out = new Uint8Array(n);
    let o = 0;
    for (const p of parts) { out.set(p, o); o += p.length; }
    return out;
}

export function download(data, name, type) {
    const url = URL.createObjectURL(new Blob([data], { type }));
    const a = document.createElement('a');
    a.href = url;
    a.download = name;
    document.body.appendChild(a);
    a.click();
    a.remove();
    setTimeout(() => URL.revokeObjectURL(url), 10000);
}

// ---- playback ----
let ctx = null, current = null;
export function unlockAudio() {
    if (!ctx) {
        const AC = globalThis.AudioContext || globalThis.webkitAudioContext;
        ctx = new AC();
    }
    if (ctx.state === 'suspended') ctx.resume();
}
export function play(x, rate) {
    unlockAudio();
    stop();
    const y = rate === 44100 ? x : resample(x, 4, 1);  // some browsers reject 11025 Hz buffers
    const buf = ctx.createBuffer(1, y.length, 44100);
    buf.getChannelData(0).set(y);
    const src = ctx.createBufferSource();
    src.buffer = buf;
    src.connect(ctx.destination);
    src.start();
    current = src;
    return y.length / 44100;
}
export function stop() {
    if (current) { try { current.stop(); } catch (e) { /* already stopped */ } current = null; }
}
