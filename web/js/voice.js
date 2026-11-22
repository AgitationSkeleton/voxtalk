// voxtalk voice pipeline (port of src/voice.cpp).
import * as dsp from './dsp.js';
import { VOX_TIMING, VOX_PITCH } from './voxdata.js';

const SR = 11025;
// Loop filter of the Half-Life VOX echo, measured from the game files.
const VOX_ECHO_TAPS = [0.30, 0.09, 0.03, 0.027];

export const SOUND_EFFECTS = ['bizwarn', 'bloop', 'buzwarn', 'dadeda', 'deeoo', 'doop', 'woop', 'endgame'];

export function voxDefaults() {
    return {
        engine: { pitch: 20, expression: 55, rate: 33, headSize: 46, prominences: 17, lowering: 0, volume: 100 },
        speed: 11025 / 12000,
        echoAmount: 1.0,
        echoDelayMs: 1510 / 11.025,
        lowCutHz: 160,
        style: 'words',
        gameTiming: true,
        gamePitch: true,
    };
}

// ------------------------------------------------------------ sound effects
const soundCache = new Map();
let soundBase = 'sounds/';
export function setSoundBase(url) { soundBase = url; }

function decodeWav(buf) {
    const dv = new DataView(buf);
    let p = 12, rate = 0, bits = 0, ch = 1;
    while (p + 8 <= dv.byteLength) {
        const id = String.fromCharCode(dv.getUint8(p), dv.getUint8(p + 1), dv.getUint8(p + 2), dv.getUint8(p + 3));
        const len = dv.getUint32(p + 4, true);
        if (id === 'fmt ') { ch = dv.getUint16(p + 10, true); rate = dv.getUint32(p + 12, true); bits = dv.getUint16(p + 22, true); }
        else if (id === 'data') {
            const n = Math.min(len, dv.byteLength - p - 8);
            let out;
            if (bits === 8) { out = new Float32Array(Math.floor(n / ch)); for (let i = 0; i < out.length; i++) out[i] = (dv.getUint8(p + 8 + i * ch) - 128) / 127; }
            else { out = new Float32Array(Math.floor(n / 2 / ch)); for (let i = 0; i < out.length; i++) out[i] = dv.getInt16(p + 8 + i * 2 * ch, true) / 32768; }
            if (rate === 22050) out = dsp.resample(out, 1, 2);
            else if (rate === 44100) out = dsp.resample(out, 1, 4);
            return out;
        }
        p += 8 + len + (len & 1);
    }
    throw new Error('bad wav');
}

export async function soundEffect(name) {
    const key = name.toLowerCase();
    if (!soundCache.has(key)) {
        const r = await fetch(soundBase + key + '.wav');
        if (!r.ok) throw new Error('Sound effect missing: ' + name);
        soundCache.set(key, decodeWav(await r.arrayBuffer()));
    }
    return soundCache.get(key).slice();
}

// ------------------------------------------------------------ parsing
const isSfx = (w) => SOUND_EFFECTS.includes(w.toLowerCase());
const pauseFor = (c) => (',;:'.includes(c) ? 0.25 : '.!?'.includes(c) ? 0.43 : 0);
const isSpace = (c) => /\s/.test(c);

function applyModifiers(grp, wp) {
    let i = 0;
    while (i < grp.length) {
        const k = grp[i].toLowerCase();
        let j = i + 1;
        while (j < grp.length && /[0-9.]/.test(grp[j])) j++;
        if (j > i + 1) {
            const v = parseFloat(grp.slice(i + 1, j));
            if (k === 's') wp.s = v; else if (k === 'e') wp.e = v; else if (k === 'p') wp.p = v;
            else if (k === 'v') wp.v = v; else if (k === 't') wp.t = v;
        }
        i = j > i + 1 ? j : i + 1;
    }
}

export function parse(text) {
    const items = [];
    const defaults = { s: 0, e: 100, p: 100, v: 100, t: 0 };
    let i = 0;
    const n = text.length;
    while (i < n) {
        const c = text[i];
        if (isSpace(c)) { i++; continue; }
        if (c === '(') {
            let j = text.indexOf(')', i);
            if (j < 0) j = n;
            applyModifiers(text.slice(i + 1, j), defaults);
            i = j + 1;
            continue;
        }
        if (pauseFor(c) > 0) { items.push({ kind: 'pause', pause: pauseFor(c), punct: c }); i++; continue; }
        let j = i;
        while (j < n && !isSpace(text[j]) && text[j] !== '(' && pauseFor(text[j]) === 0) j++;
        let w = text.slice(i, j);
        i = j;
        const slash = Math.max(w.lastIndexOf('/'), w.lastIndexOf('\\'));
        if (slash >= 0) w = w.slice(slash + 1);
        const wp = { ...defaults };
        if (i < n && text[i] === '(') {
            let k = text.indexOf(')', i);
            if (k < 0) k = n;
            applyModifiers(text.slice(i + 1, k), wp);
            i = k + 1;
        }
        let clean = '';
        for (const ch of w) if (/[\p{L}\p{N}'_-]/u.test(ch)) clean += ch;
        clean = clean.replace(/[-']+$/, '');
        if (!clean) continue;
        if (clean.toLowerCase() === '_comma') { items.push({ kind: 'pause', pause: 0.25, punct: ',' }); continue; }
        if (clean.toLowerCase() === '_period') { items.push({ kind: 'pause', pause: 0.43, punct: '.' }); continue; }
        const kind = isSfx(clean) ? 'sound' : 'word';
        items.push({ kind, text: kind === 'word' ? clean.replace(/_/g, ' ') : clean, wp });
    }
    return items;
}

function applyWordParams(x, wp) {
    if (!x.length) return x;
    const s = Math.max(0, Math.min(100, wp.s)), e = Math.max(s, Math.min(100, wp.e));
    if (s > 0 || e < 100) {
        const a = Math.floor(x.length * s / 100), b = Math.floor(x.length * e / 100);
        x = x.slice(a, Math.max(a, b));
    }
    if (wp.t > 0 && wp.t < 100 && x.length > 512) {
        const slice = 220, fade = 32, keep = 1 - wp.t / 100;
        const y = [];
        let acc = 0, skipped = false;
        for (let p = 0; p < x.length; p += slice) {
            acc += keep;
            if (acc < 1 && p > 0) { skipped = true; continue; }
            acc -= 1;
            const end = Math.min(x.length, p + slice);
            let q = p;
            if (skipped && y.length >= fade) {
                const base = y.length - fade;
                for (let f = 0; f < fade && q < end; f++, q++) {
                    const g = (f + 1) / (fade + 1);
                    y[base + f] = y[base + f] * (1 - g) + x[q] * g;
                }
            }
            for (; q < end; q++) y.push(x[q]);
            skipped = false;
        }
        x = Float32Array.from(y);
    }
    if (wp.p > 0 && Math.abs(wp.p - 100) > 0.5) x = dsp.stretch(x, 100 / wp.p);
    if (Math.abs(wp.v - 100) > 0.5) { x = x.slice(); for (let i = 0; i < x.length; i++) x[i] *= wp.v / 100; }
    return x;
}

const keyOf = (word) => (/^[\x00-\x7f]*$/.test(word) ? word.toLowerCase().replace(/ /g, '_') : null);

// ------------------------------------------------------------ game pitch contour
function applyGamePitch(x, word, sp) {
    const key = keyOf(word);
    const target = key && VOX_PITCH[key];
    if (!target) return x;
    const HOP = 64;
    let tr = dsp.pitchTrack(x, HOP, SR, 35, 250);
    {
        const c = tr.slice();
        for (let k = 0; k < tr.length; k++) {
            if (tr[k] <= 0) continue;
            const nb = [];
            for (let j = k - 4; j <= k + 4; j++) if (j >= 0 && j < tr.length && tr[j] > 0) nb.push(tr[j]);
            nb.sort((a, b) => a - b);
            const med = nb[nb.length >> 1];
            if (Math.abs(12 * Math.log2(tr[k] / med)) > 4) c[k] = med;
        }
        tr = c;
    }
    let v0 = -1, v1 = -1;
    for (let k = 0; k < tr.length; k++) if (tr[k] > 0) { if (v0 < 0) v0 = k; v1 = k; }
    if (v0 < 0 || v1 - v0 < 6) return x;
    const NS = target.length;
    const src = new Array(NS), tgt = new Array(NS);
    for (let q = 0; q < NS; q++) {
        const v = [];
        for (let k = v0; k <= v1; k++) {
            const u = (k - v0) / (v1 - v0);
            if (tr[k] > 0 && u >= q / NS && u <= (q + 1) / NS) v.push(12 * Math.log2(tr[k] / 100));
        }
        if (!v.length) { if (q === 0) return x; src[q] = src[q - 1]; continue; }
        v.sort((a, b) => a - b);
        src[q] = v[v.length >> 1];
    }
    let mean = 0;
    for (let q = 0; q < NS; q++) { tgt[q] = target[q] / 5; mean += tgt[q] / NS; }
    const scale = Math.max(0, sp.expression / 55), offset = (sp.pitch - 20) * 0.37;
    for (let q = 0; q < NS; q++) tgt[q] = mean + (tgt[q] - mean) * scale + offset;
    const interp = (v, u) => {
        const pos = u * NS - 0.5;
        if (pos <= 0) return v[0];
        if (pos >= NS - 1) return v[NS - 1];
        const i = Math.floor(pos), f = pos - i;
        return v[i] + (v[i + 1] - v[i]) * f;
    };
    const d = new Float64Array(tr.length);
    for (let k = 0; k < tr.length; k++) {
        const u = Math.max(0, Math.min(0.75, (k - v0) / (v1 - v0)));
        d[k] = Math.max(-4, Math.min(4, interp(tgt, u) - interp(src, u)));
    }
    const ratio = new Float32Array(tr.length);
    for (let k = 0; k < tr.length; k++) {
        let acc = 0, n = 0;
        for (let j = k - 4; j <= k + 4; j++) if (j >= 0 && j < d.length) { acc += d[j]; n++; }
        ratio[k] = Math.pow(2, acc / n / 12);
    }
    return dsp.psola(x, tr, HOP, SR, ratio);
}

// ------------------------------------------------------------ rendering
async function renderChunk(engine, vs, text, wordMode) {
    const spoken = wordMode ? '\\!*L*+H ' + text : text;
    const pcm = await engine.speak(vs.engine, spoken);
    let x = new Float32Array(pcm.length);
    for (let i = 0; i < pcm.length; i++) x[i] = pcm[i] / 32768;
    x = dsp.trim(x, -55, 32, 220);
    if (!x.length) return x;
    if (Math.abs(vs.speed - 11025 / 12000) < 1e-6) x = dsp.resample(x, 160, 147);
    else if (vs.speed > 0.05) x = dsp.stretch(x, 1 / vs.speed);
    if (wordMode && vs.gamePitch) x = applyGamePitch(x, text, vs.engine);
    dsp.highpass(x, vs.lowCutHz, SR, 4);
    x = dsp.feedbackEcho(x, Math.round(vs.echoDelayMs * SR / 1000), VOX_ECHO_TAPS, vs.echoAmount);
    const key = keyOf(text);
    const target = wordMode && vs.gameTiming && key ? VOX_TIMING[key] || 0 : 0;
    if (target) {
        x = dsp.trim(x, -60, 0, 0);
        if (x.length > target) {
            x = x.slice(0, target);
            const fade = Math.min(110, target);
            for (let i = 0; i < fade; i++) x[target - fade + i] *= (fade - i) / fade;
        } else {
            const y = new Float32Array(target);
            y.set(x);
            x = y;
        }
    } else {
        x = dsp.trim(x, wordMode ? -29 : -60, 0, wordMode ? 0 : 110);
    }
    dsp.normalize(x, 0.95);
    return x;
}

function concat(parts) {
    let n = 0;
    for (const p of parts) n += p.length;
    const out = new Float32Array(n);
    let o = 0;
    for (const p of parts) { out.set(p, o); o += p.length; }
    return out;
}

export function splitWords(text) {
    return parse(text).filter((it) => it.kind === 'word').map((it) => ({
        text: it.text,
        stem: it.text.toLowerCase().replace(/[ -]/g, '_').replace(/[^\p{L}\p{N}_]/gu, ''),
    }));
}

export function suggestFileName(text, maxLen = 80) {
    let name = '';
    for (const it of parse(text)) {
        if (it.kind === 'pause') continue;
        let stem = it.text.toLowerCase().replace(/[ _-]/g, '_').replace(/[^a-z0-9_]/g, '');
        if (!stem) continue;
        if (name && name.length + 1 + stem.length > maxLen) break;
        if (!name && stem.length > maxLen) stem = stem.slice(0, maxLen);
        name += (name ? '_' : '') + stem;
    }
    return name || 'voxtalk';
}

// Render text to Float32Array at opts.sampleRate (11025 or 44100).
// `signal` (an AbortSignal) cancels between engine requests.
export async function render(engine, vs, opts, text, onProgress, signal) {
    const check = () => { if (signal && signal.aborted) throw new DOMException('Stopped', 'AbortError'); };
    const items = parse(text);
    const parts = [];
    if (vs.style === 'sentence') {
        let run = '';
        const flush = async () => {
            check();
            if (/[\p{L}\p{N}]/u.test(run)) parts.push(await renderChunk(engine, vs, run, false));
            run = '';
        };
        for (const it of items) {
            if (it.kind === 'word') run += (run ? ' ' : '') + it.text;
            else if (it.kind === 'pause') run += it.punct;
            else { await flush(); parts.push(await soundEffect(it.text)); }
        }
        await flush();
    } else {
        let done = 0;
        for (const it of items) {
            check();
            if (it.kind === 'pause') { parts.push(new Float32Array(Math.floor(it.pause * SR))); continue; }
            let b = it.kind === 'sound' ? await soundEffect(it.text) : await renderChunk(engine, vs, it.text, true);
            parts.push(applyWordParams(b, it.wp));
            if (onProgress) onProgress(++done, items.length);
        }
    }
    check();
    let all = concat(parts);
    if (!all.length) throw new Error('Nothing to say.');
    for (let i = 0; i < all.length; i++) all[i] = Math.max(-128 / 127, Math.min(1, all[i]));
    if (opts.eightBit) dsp.quantize8(all, 777);
    if (opts.sampleRate === 44100) all = dsp.resample(all, 4, 1);
    return all;
}
