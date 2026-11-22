// DSP for voxtalk (port of src/dsp.cpp). Buffers are Float32Array.
const PI = Math.PI;

function besselI0(x) {
    let s = 1, t = 1;
    for (let k = 1; k < 40; k++) { t *= (x / (2 * k)) * (x / (2 * k)); s += t; }
    return s;
}

let WIN = null;
function kaiserTable() {
    if (WIN) return WIN;
    const TAB = 4096, beta = 8.0, i0b = besselI0(beta);
    WIN = new Float64Array(TAB + 2);
    for (let i = 0; i <= TAB + 1; i++) {
        const u = Math.min(1, i / TAB);
        WIN[i] = besselI0(beta * Math.sqrt(Math.max(0, 1 - u * u))) / i0b;
    }
    return WIN;
}

function interpolate(x, nout, step, cutoff) {
    const half = 24, TAB = 4096;
    const scale = Math.min(1, cutoff);
    const halfw = Math.ceil(half / scale);
    const win = kaiserTable();
    const y = new Float32Array(nout);
    const N = x.length;
    for (let n = 0; n < nout; n++) {
        const t = n * step;
        const c = Math.floor(t);
        let acc = 0;
        const lo = Math.max(0, c - halfw + 1), hi = Math.min(N - 1, c + halfw);
        for (let i = lo; i <= hi; i++) {
            const d = t - i;
            const u = Math.abs(d) / halfw;
            if (u >= 1) continue;
            const fi = u * TAB, k = fi | 0;
            const w = win[k] + (win[k + 1] - win[k]) * (fi - k);
            const a = scale * d;
            const s = Math.abs(a) < 1e-9 ? scale : scale * Math.sin(PI * a) / (PI * a);
            acc += x[i] * s * w;
        }
        y[n] = acc;
    }
    return y;
}

export function resample(x, up, down) {
    if (up === down || !x.length) return x;
    const nout = Math.floor(x.length * up / down);
    const ratio = up / down;
    return interpolate(x, nout, 1 / ratio, Math.min(1, ratio) * 0.97);
}

export function stretch(x, factor) {
    if (Math.abs(factor - 1) < 1e-6 || !x.length) return x;
    const nout = Math.floor(x.length * factor);
    return interpolate(x, nout, 1 / factor, Math.min(1, factor) * 0.97);
}

function hpSection(fc, sr, q) {
    const w = 2 * PI * fc / sr, cw = Math.cos(w), al = Math.sin(w) / (2 * q), a0 = 1 + al;
    return { b0: (1 + cw) / 2 / a0, b1: -(1 + cw) / a0, b2: (1 + cw) / 2 / a0, a1: -2 * cw / a0, a2: (1 - al) / a0 };
}

export function highpass(x, fc, sr, order) {
    if (fc <= 0) return;
    const secs = order >= 4 ? [hpSection(fc, sr, 0.5412), hpSection(fc, sr, 1.3066)] : [hpSection(fc, sr, 0.7071)];
    for (const s of secs) {
        let z1 = 0, z2 = 0;
        for (let i = 0; i < x.length; i++) {
            const v = x[i];
            const out = s.b0 * v + z1;
            z1 = s.b1 * v - s.a1 * out + z2;
            z2 = s.b2 * v - s.a2 * out;
            x[i] = out;
        }
    }
}

export function feedbackEcho(x, delay, taps, amount) {
    if (amount <= 0 || delay <= 0 || !taps.length) return x;
    let g = 0;
    for (const t of taps) g += Math.abs(t) * amount;
    let repeats = g < 0.999 ? Math.ceil(-60 / (20 * Math.log10(Math.max(g, 1e-3)))) : 40;
    repeats = Math.min(repeats, 40);
    const y = new Float32Array(x.length + (delay + taps.length) * repeats);
    y.set(x);
    for (let n = delay; n < y.length; n++) {
        let acc = 0;
        for (let k = 0; k < taps.length; k++) if (n >= delay + k) acc += taps[k] * y[n - delay - k];
        y[n] += amount * acc;
    }
    return y;
}

export function normalize(x, peak) {
    let m = 0;
    for (const v of x) m = Math.max(m, Math.abs(v));
    if (m < 1e-9) return;
    const g = peak / m;
    for (let i = 0; i < x.length; i++) x[i] *= g;
}

export function trim(x, thresholdDb, padStart, padEnd) {
    if (!x.length) return x;
    const W = 64;
    let peak = 0;
    for (const v of x) peak = Math.max(peak, Math.abs(v));
    if (peak <= 0) return new Float32Array(0);
    const thr = peak * Math.pow(10, thresholdDb / 20);
    const env = new Float32Array(x.length);
    let acc = 0;
    for (let i = 0; i < x.length; i++) {
        acc += Math.abs(x[i]);
        if (i >= W) acc -= Math.abs(x[i - W]);
        env[i] = acc / W;
    }
    let first = -1, last = -1;
    for (let i = 0; i < x.length; i++) if (env[i] > thr) { first = i - W; break; }
    for (let i = x.length - 1; i >= 0; i--) if (env[i] > thr) { last = i; break; }
    if (first < 0) first = 0;
    if (last < 0) return new Float32Array(0);
    const a = Math.max(0, first - padStart), b = Math.min(x.length, last + 1 + padEnd);
    return x.slice(a, b);
}

export function quantize8(x, seed) {
    let s = (seed >>> 0) || 12345;
    const urand = () => {
        s ^= s << 13; s >>>= 0; s ^= s >>> 17; s ^= s << 5; s >>>= 0;
        return (s >>> 8) / 16777216;
    };
    for (let i = 0; i < x.length; i++) {
        const g = x[i] * 127;
        if (Math.abs(g - Math.round(g)) < 1e-3) { x[i] = Math.max(-128, Math.min(127, Math.round(g))) / 127; continue; }
        const d = (urand() - urand()) * 0.5;
        const q = Math.max(-128, Math.min(127, Math.round(x[i] * 127 + d)));
        x[i] = q / 127;
    }
}

export function pitchTrack(x, hop, sr, fmin, fmax) {
    const minLag = Math.floor(sr / fmax), maxLag = Math.floor(sr / fmin) + 1;
    const W = maxLag * 2, N = x.length;
    const frames = Math.floor(N / hop) + 1;
    const f0 = new Float32Array(frames);
    const en = new Float64Array(frames);
    let emax = 0;
    for (let k = 0; k < frames; k++) {
        const c = k * hop;
        let e = 0;
        for (let n = Math.max(0, c - W / 2); n < Math.min(N, c + W / 2); n++) e += x[n] * x[n];
        en[k] = e;
        emax = Math.max(emax, e);
    }
    const r = new Float64Array(maxLag + 2);
    for (let k = 0; k < frames; k++) {
        if (en[k] < emax * 0.003) continue;
        const a = k * hop - W / 2;
        let best = 0;
        for (let l = minLag; l <= maxLag; l++) {
            let num = 0, e1 = 0, e2 = 0;
            for (let n = 0; n < W - l; n++) {
                const i = a + n, j = a + n + l;
                if (i < 0 || j >= N) continue;
                num += x[i] * x[j]; e1 += x[i] * x[i]; e2 += x[j] * x[j];
            }
            r[l] = e1 > 0 && e2 > 0 ? num / Math.sqrt(e1 * e2) : 0;
            best = Math.max(best, r[l]);
        }
        if (best < 0.55) continue;
        let pick = -1;
        for (let l = minLag + 1; l < maxLag; l++)
            if (r[l] >= r[l - 1] && r[l] >= r[l + 1] && r[l] >= 0.88 * best) { pick = l; break; }
        if (pick < 0) continue;
        const a0 = r[pick - 1], b0 = r[pick], c0 = r[pick + 1];
        const den = a0 - 2 * b0 + c0, off = Math.abs(den) > 1e-12 ? 0.5 * (a0 - c0) / den : 0;
        f0[k] = sr / (pick + Math.max(-0.5, Math.min(0.5, off)));
    }
    const sm = f0.slice();
    for (let k = 0; k < frames; k++) {
        if (f0[k] <= 0) continue;
        const v = [];
        for (let j = k - 2; j <= k + 2; j++) if (j >= 0 && j < frames && f0[j] > 0) v.push(f0[j]);
        v.sort((p, q) => p - q);
        sm[k] = v[v.length >> 1];
    }
    for (let k = 1; k < frames; k++) {
        if (sm[k] > 0 || sm[k - 1] <= 0) continue;
        let j = k;
        while (j < frames && sm[j] <= 0) j++;
        if (j < frames && j - k <= 4) {
            for (let i = k; i < j; i++) {
                const f = (i - k + 1) / (j - k + 1);
                sm[i] = sm[k - 1] * (1 - f) + sm[j] * f;
            }
        }
        k = j;
    }
    return sm;
}

export function psola(x, track, hop, sr, ratio) {
    const N = x.length;
    const periodAt = (n) => { const k = Math.max(0, Math.min(track.length - 1, Math.floor(n / hop))); return track[k] > 0 ? sr / track[k] : 0; };
    const ratioAt = (n) => ratio[Math.max(0, Math.min(ratio.length - 1, Math.floor(n / hop)))];
    // zero-phase low-pass copy for pitch marks
    const lp = x.slice();
    {
        const w = 2 * PI * 900 / sr, cw = Math.cos(w), al = Math.sin(w) / (2 * 0.7071), a0 = 1 + al;
        const b0 = (1 - cw) / 2 / a0, b1 = (1 - cw) / a0, a1 = -2 * cw / a0, a2 = (1 - al) / a0;
        let z1 = 0, z2 = 0;
        for (let i = 0; i < N; i++) { const v = lp[i], o = b0 * v + z1; z1 = b1 * v - a1 * o + z2; z2 = b0 * v - a2 * o; lp[i] = o; }
        z1 = z2 = 0;
        for (let i = N - 1; i >= 0; i--) { const v = lp[i], o = b0 * v + z1; z1 = b1 * v - a1 * o + z2; z2 = b0 * v - a2 * o; lp[i] = o; }
    }
    const marks = [];
    let n = 0;
    while (n < N) {
        const T = periodAt(n);
        if (T <= 0) { n += hop; continue; }
        if (!marks.length || n - marks[marks.length - 1] > 2 * T) {
            let best = n;
            for (let i = n; i < Math.min(N, n + Math.floor(T)); i++) if (lp[i] > lp[best]) best = i;
            marks.push(best);
            n = best + Math.floor(T);
            continue;
        }
        const lo = Math.max(0, n - Math.floor(T * 0.2)), hi = Math.min(N - 1, n + Math.floor(T * 0.2));
        let best = lo;
        for (let i = lo; i <= hi; i++) if (lp[i] > lp[best]) best = i;
        marks.push(best);
        n = best + Math.floor(Math.max(8, T));
    }
    if (marks.length < 3) return x;
    const out = new Float32Array(N), mask = new Float32Array(N);
    const localT = (i) => { let T = periodAt(marks[i]); if (T <= 0 && i > 0) T = marks[i] - marks[i - 1]; return Math.max(8, T); };
    let m = 0;
    while (m < marks.length) {
        let e = m;
        while (e + 1 < marks.length && marks[e + 1] - marks[e] < 2 * localT(e) + 2) e++;
        if (e - m >= 2) {
            const r0 = Math.max(0, marks[m] - Math.floor(localT(m))), r1 = Math.min(N - 1, marks[e] + Math.floor(localT(e)));
            for (let i = r0; i <= r1; i++) mask[i] = 1;
            let ts = marks[m];
            while (ts <= marks[e]) {
                let k = m;
                while (k < e && Math.abs(marks[k + 1] - ts) < Math.abs(marks[k] - ts)) k++;
                const mk = marks[k];
                const Tn = k < e ? marks[k + 1] - mk : 1e9, Tp = k > m ? mk - marks[k - 1] : 1e9;
                const Tm = Math.min(Tn, Tp);
                const Ta = k < e ? Tn : Tp;
                const rr = Math.max(0.5, Math.min(2, ratioAt(ts)));
                const L = Math.floor(Tm), gain = 1 / rr, dc = Math.round(ts);
                for (let i = -L; i <= L; i++) {
                    const src = mk + i, dst = dc + i;
                    if (src < 0 || src >= N || dst < 0 || dst >= N) continue;
                    const w = 0.5 * (1 + Math.cos(PI * i / (L + 1)));
                    out[dst] += x[src] * w * gain;
                }
                ts += Ta / rr;
            }
        }
        m = e + 1;
    }
    const S = 64, ms = new Float32Array(N);
    let acc = 0;
    for (let i = 0; i < N + S / 2; i++) {
        if (i < N) acc += mask[i];
        if (i - S >= 0) acc -= mask[i - S];
        const c = i - S / 2;
        if (c >= 0 && c < N) ms[c] = acc / S;
    }
    const y = new Float32Array(N);
    for (let i = 0; i < N; i++) y[i] = out[i] * ms[i] + x[i] * (1 - ms[i]);
    return y;
}
